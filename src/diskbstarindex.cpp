
#include "diskbstarindex.h"
#include "securefile.h"
#include "storage.h"
#include "stringpool.h"
#include "utils.h"

#include <algorithm>
#include <stdexcept>

// Конструктор: инициализирует пути, тип ключа и проверяет файлы
DiskBStarIndex::DiskBStarIndex(
    const std::filesystem::path& pageFilePath,
    const std::filesystem::path& metaFilePath,
    ColumnType keyType
)
    : pageFilePath_(pageFilePath), metaFilePath_(metaFilePath), keyType_(keyType)
{
    ensureFilesExist();
}

// Создает файлы индекса и метаданных, если они отсутствуют
void DiskBStarIndex::ensureFilesExist() const
{
    if (!secureFileExists(pageFilePath_))
    {
        createEmptySecureFile(pageFilePath_);
    }

    if (!secureFileExists(metaFilePath_))
    {
        HeaderData header;
        saveHeader(header);
    }
}

// Полностью очищает индекс и перезаписывает файлы
void DiskBStarIndex::clear()
{
    std::vector<std::string> emptyPages;
    writeSecureRecords(pageFilePath_, emptyPages);
    HeaderData header;
    saveHeader(header);
}

// Загружает и парсит заголовок метаданных из файла
DiskBStarIndex::HeaderData DiskBStarIndex::loadHeader() const
{
    ensureFilesExist();

    HeaderData header;
    std::vector<std::string> records = readSecureRecords(metaFilePath_);
    if (records.empty())
    {
        return header;
    }

    ProtoBStarDiskHeader proto;
    if (!proto.ParseFromString(records[0]))
    {
        throw std::runtime_error("не удалось прочитать meta-файл файлового B*-индекса");
    }

    // Заполнение основных полей заголовка
    header.rootExists = proto.root_exists();
    header.rootPageId = proto.root_page_id();
    header.nextPageId = proto.next_page_id();
    header.size = proto.size();

    // Загрузка карты расположения страниц на диске
    for (int index = 0; index < proto.locations_size(); ++index)
    {
        const ProtoBStarPageLocation& location = proto.locations(index);
        PageLocation item;
        item.offset = static_cast<std::streamoff>(location.offset());
        item.size = static_cast<std::streamoff>(location.size());
        header.locations[location.page_id()] = item;
    }

    // Загрузка списка свободных слотов для повторного использования
    for (int index = 0; index < proto.free_slots_size(); ++index)
    {
        const ProtoBStarFreeSlot& slot = proto.free_slots(index);
        FreeSlot item;
        item.offset = static_cast<std::streamoff>(slot.offset());
        item.size = static_cast<std::streamoff>(slot.size());
        header.freeSlots.push_back(item);
    }

    return header;
}

// Сериализует и сохраняет заголовок метаданных в файл
void DiskBStarIndex::saveHeader(const HeaderData& header) const
{
    ProtoBStarDiskHeader proto;
    proto.set_root_exists(header.rootExists);
    proto.set_root_page_id(header.rootPageId);
    proto.set_next_page_id(header.nextPageId);
    proto.set_size(header.size);

    // Сохранение расположения страниц
    for (std::map<long long, PageLocation>::const_iterator it = header.locations.begin(); it != header.locations.end(); ++it)
    {
        ProtoBStarPageLocation* location = proto.add_locations();
        location->set_page_id(it->first);
        location->set_offset(static_cast<long long>(it->second.offset));
        location->set_size(static_cast<long long>(it->second.size));
    }

    // Сохранение свободных слотов
    for (std::size_t index = 0; index < header.freeSlots.size(); ++index)
    {
        ProtoBStarFreeSlot* slot = proto.add_free_slots();
        slot->set_offset(static_cast<long long>(header.freeSlots[index].offset));
        slot->set_size(static_cast<long long>(header.freeSlots[index].size));
    }

    std::string bytes;
    if (!proto.SerializeToString(&bytes))
    {
        throw std::runtime_error("не удалось сериализовать meta-файл B*-индекса");
    }

    std::vector<std::string> records;
    records.push_back(bytes);
    writeSecureRecords(metaFilePath_, records);
}

// Создает и инициализирует новую структуру страницы
ProtoBStarPage DiskBStarIndex::makePage(HeaderData& header, bool isLeaf) const
{
    ProtoBStarPage page;
    page.set_page_id(header.nextPageId);
    header.nextPageId += 1; // Инкремент счетчика ID страниц
    page.set_is_leaf(isLeaf);
    page.set_deleted(false);
    return page;
}

// Читает страницу с диска по ее ID и валидирует данные
ProtoBStarPage DiskBStarIndex::readPage(const HeaderData& header, long long pageId) const
{
    std::map<long long, PageLocation>::const_iterator found = header.locations.find(pageId);
    if (found == header.locations.end())
    {
        throw std::runtime_error("в файловом B*-индексе нет страницы page_id=" + std::to_string(pageId));
    }

    std::string bytes = readSecureRecordAtOffset(pageFilePath_, found->second.offset);
    ProtoBStarPage page;
    if (!page.ParseFromString(bytes))
    {
        throw std::runtime_error("не удалось прочитать страницу B*-индекса из .tree.pb");
    }

    if (page.deleted())
    {
        throw std::runtime_error("попытка прочитать tombstone-страницу B*-индекса");
    }

    return page;
}

// Записывает страницу на диск, оптимизируя место через свободные слоты
void DiskBStarIndex::writePage(HeaderData& header, const ProtoBStarPage& page) const
{
    std::string bytes;
    if (!page.SerializeToString(&bytes))
    {
        throw std::runtime_error("не удалось сериализовать страницу файлового B*-индекса");
    }

    // Попытка перезаписать страницу в ее текущий слот
    std::map<long long, PageLocation>::iterator current = header.locations.find(page.page_id());
    if (current != header.locations.end())
    {
        if (overwriteSecureRecordInSlot(pageFilePath_, bytes, current->second.offset, current->second.size))
        {
            return;
        }

        // Если не поместилась, старый слот освобождается
        header.freeSlots.push_back(FreeSlot{current->second.offset, current->second.size});
        header.locations.erase(current);
    }

    // Поиск подходящего по размеру свободного слота среди удаленных
    for (std::size_t index = 0; index < header.freeSlots.size(); ++index)
    {
        if (overwriteSecureRecordInSlot(pageFilePath_, bytes, header.freeSlots[index].offset, header.freeSlots[index].size))
        {
            PageLocation location;
            location.offset = header.freeSlots[index].offset;
            location.size = header.freeSlots[index].size;
            header.locations[page.page_id()] = location;
            header.freeSlots.erase(header.freeSlots.begin() + static_cast<std::ptrdiff_t>(index));
            return;
        }
    }

    // Если слотов нет, данные дописываются в конец файла
    std::streamoff offset = 0;
    appendSecureRecord(pageFilePath_, bytes, &offset);
    PageLocation location;
    location.offset = offset;
    location.size = secureRecordTotalSizeAtOffset(pageFilePath_, offset);
    header.locations[page.page_id()] = location;
}

// Освобождает занятый страницей слот и переносит его в список свободных
void DiskBStarIndex::freeOldPageSlot(HeaderData& header, long long pageId) const
{
    std::map<long long, PageLocation>::iterator found = header.locations.find(pageId);
    if (found == header.locations.end())
    {
        return;
    }
    header.freeSlots.push_back(FreeSlot{found->second.offset, found->second.size});
    header.locations.erase(found);
}

// Преобразует ключ записи из Protobuf во внутренний формат Value
Value DiskBStarIndex::entryKey(const ProtoIndexEntry& entry) const
{
    return valueFromProto(entry.key(), keyType_);
}

// Создает Protobuf-объект записи индекса
ProtoIndexEntry DiskBStarIndex::makeEntry(const Value& key, std::streamoff offset, bool deleted) const
{
    ProtoIndexEntry entry;
    *entry.mutable_key() = valueToProto(key);
    entry.set_offset(static_cast<long long>(offset));
    entry.set_deleted(deleted);
    return entry;
}

// Сравнивает переданный ключ с ключом из записи
int DiskBStarIndex::compareKeyWithEntry(const Value& key, const ProtoIndexEntry& entry) const
{
    Value right = entryKey(entry);
    return compareValues(key, right);
}

// Ищет позицию для вставки ключа в массив записей страницы (линейный поиск)
int DiskBStarIndex::findEntryPosition(const ProtoBStarPage& page, const Value& key) const
{
    int index = 0;
    while (index < page.entries_size() && compareKeyWithEntry(key, page.entries(index)) > 0)
    {
        ++index;
    }
    return index;
}

// Копирует все записи страницы в std::vector
std::vector<ProtoIndexEntry> DiskBStarIndex::pageEntries(const ProtoBStarPage& page) const
{
    std::vector<ProtoIndexEntry> entries;
    for (int index = 0; index < page.entries_size(); ++index)
    {
        entries.push_back(page.entries(index));
    }
    return entries;
}

// Копирует ID всех дочерних страниц в std::vector
std::vector<long long> DiskBStarIndex::pageChildren(const ProtoBStarPage& page) const
{
    std::vector<long long> children;
    for (int index = 0; index < page.child_page_ids_size(); ++index)
    {
        children.push_back(page.child_page_ids(index));
    }
    return children;
}

// Полностью заменяет записи в Protobuf-странице
void DiskBStarIndex::replaceEntries(ProtoBStarPage& page, const std::vector<ProtoIndexEntry>& entries) const
{
    page.clear_entries();
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        *page.add_entries() = entries[index];
    }
}

// Полностью заменяет указатели на детей в Protobuf-странице
void DiskBStarIndex::replaceChildren(ProtoBStarPage& page, const std::vector<long long>& children) const
{
    page.clear_child_page_ids();
    for (std::size_t index = 0; index < children.size(); ++index)
    {
        page.add_child_page_ids(children[index]);
    }
}

// Удаляет логически удаленные записи (tombstones) из листа для очистки места
void DiskBStarIndex::compactLeafTombstones(ProtoBStarPage& page) const
{
    if (!page.is_leaf())
    {
        return;
    }

    std::vector<ProtoIndexEntry> active;
    for (int index = 0; index < page.entries_size(); ++index)
    {
        if (!page.entries(index).deleted())
        {
            active.push_back(page.entries(index));
        }
    }
    replaceEntries(page, active);
}

// Рекурсивный поиск смещения данных по ключу внутри конкретной страницы
std::optional<std::streamoff> DiskBStarIndex::findInPage(const HeaderData& header, long long pageId, const Value& key) const
{
    ProtoBStarPage page = readPage(header, pageId);
    const int position = findEntryPosition(page, key);

    // Ключ найден на текущей странице
    if (position < page.entries_size() && compareKeyWithEntry(key, page.entries(position)) == 0)
    {
        if (page.entries(position).deleted())
        {
            return std::nullopt;
        }
        return static_cast<std::streamoff>(page.entries(position).offset());
    }

    if (page.is_leaf())
    {
        return std::nullopt;
    }

    if (position >= page.child_page_ids_size())
    {
        return std::nullopt;
    }

    // Рекурсивный спуск к дочерней странице
    return findInPage(header, page.child_page_ids(position), key);
}

// Публичный метод поиска: загружает заголовок и запускает поиск от корня
std::optional<std::streamoff> DiskBStarIndex::find(const Value& key) const
{
    HeaderData header = loadHeader();
    if (!header.rootExists)
    {
        return std::nullopt;
    }
    return findInPage(header, header.rootPageId, key);
}

// Проверяет, существует ли активный ключ в индексе
bool DiskBStarIndex::contains(const Value& key) const
{
    return find(key).has_value();
}

// Перераспределяет элементы между переполненным узлом и его левым соседом
bool DiskBStarIndex::redistributeWithLeftForInsert(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex <= 0)
    {
        return false;
    }

    ProtoBStarPage left = readPage(header, parent.child_page_ids(childIndex - 1));
    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    if (left.is_leaf() != child.is_leaf() || left.entries_size() >= kMaxEntries)
    {
        return false;
    }

    // Сборка общего массива элементов (левый + разделитель из родителя + текущий)
    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> leftEntries = pageEntries(left);
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    combined.insert(combined.end(), leftEntries.begin(), leftEntries.end());
    combined.push_back(parent.entries(childIndex - 1));
    combined.insert(combined.end(), childEntries.begin(), childEntries.end());

    const int remaining = static_cast<int>(combined.size()) - 1;
    const int newLeftCount = remaining / 2;
    const int separatorIndex = newLeftCount;

    // Равномерное деление элементов и обновление родительского разделителя
    std::vector<ProtoIndexEntry> newLeftEntries(combined.begin(), combined.begin() + newLeftCount);
    std::vector<ProtoIndexEntry> newChildEntries(combined.begin() + separatorIndex + 1, combined.end());
    replaceEntries(left, newLeftEntries);
    replaceEntries(child, newChildEntries);
    *parent.mutable_entries(childIndex - 1) = combined[separatorIndex];

    // Перераспределение дочерних указателей для внутренних узлов
    if (!child.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> leftChildren = pageChildren(left);
        std::vector<long long> childChildren = pageChildren(child);
        combinedChildren.insert(combinedChildren.end(), leftChildren.begin(), leftChildren.end());
        combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());

        std::vector<long long> newLeftChildren(combinedChildren.begin(), combinedChildren.begin() + newLeftCount + 1);
        std::vector<long long> newChildChildren(combinedChildren.begin() + newLeftCount + 1, combinedChildren.end());
        replaceChildren(left, newLeftChildren);
        replaceChildren(child, newChildChildren);
    }

    writePage(header, left);
    writePage(header, child);
    return true;
}

// Перераспределяет элементы между переполненным узлом и его правым соседом
bool DiskBStarIndex::redistributeWithRightForInsert(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex + 1 >= parent.child_page_ids_size())
    {
        return false;
    }

    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    ProtoBStarPage right = readPage(header, parent.child_page_ids(childIndex + 1));
    if (right.is_leaf() != child.is_leaf() || right.entries_size() >= kMaxEntries)
    {
        return false;
    }

    // Сборка общего массива элементов (текущий + разделитель из родителя + правый)
    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    std::vector<ProtoIndexEntry> rightEntries = pageEntries(right);
    combined.insert(combined.end(), childEntries.begin(), childEntries.end());
    combined.push_back(parent.entries(childIndex));
    combined.insert(combined.end(), rightEntries.begin(), rightEntries.end());

    const int remaining = static_cast<int>(combined.size()) - 1;
    const int newChildCount = remaining / 2;
    const int separatorIndex = newChildCount;

    // Равномерное деление элементов и обновление родительского разделителя
    std::vector<ProtoIndexEntry> newChildEntries(combined.begin(), combined.begin() + newChildCount);
    std::vector<ProtoIndexEntry> newRightEntries(combined.begin() + separatorIndex + 1, combined.end());
    replaceEntries(child, newChildEntries);
    replaceEntries(right, newRightEntries);
    *parent.mutable_entries(childIndex) = combined[separatorIndex];

    // Перераспределение дочерних указателей для внутренних узлов
    if (!child.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> childChildren = pageChildren(child);
        std::vector<long long> rightChildren = pageChildren(right);
        combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());
        combinedChildren.insert(combinedChildren.end(), rightChildren.begin(), rightChildren.end());

        std::vector<long long> newChildChildren(combinedChildren.begin(), combinedChildren.begin() + newChildCount + 1);
        std::vector<long long> newRightChildren(combinedChildren.begin() + newChildCount + 1, combinedChildren.end());
        replaceChildren(child, newChildChildren);
        replaceChildren(right, newRightChildren);
    }

    writePage(header, child);
    writePage(header, right);
    return true;
}

// Сплит B*-дерева: превращает 2 переполненных узла (левый + текущий) в 3 заполненных на 2/3
bool DiskBStarIndex::splitTwoToThreeWithLeft(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex <= 0)
    {
        return false;
    }

    ProtoBStarPage left = readPage(header, parent.child_page_ids(childIndex - 1));
    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    if (left.is_leaf() != child.is_leaf() || left.entries_size() < kMaxEntries || child.entries_size() < kMaxEntries)
    {
        return false;
    }

    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> leftEntries = pageEntries(left);
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    combined.insert(combined.end(), leftEntries.begin(), leftEntries.end());
    combined.push_back(parent.entries(childIndex - 1));
    combined.insert(combined.end(), childEntries.begin(), childEntries.end());

    // Расчет пропорций деления на 3 части и 2 разделителя
    const int total = static_cast<int>(combined.size());
    const int remaining = total - 2;
    const int leftCount = remaining / 3;
    const int middleCount = remaining / 3;
    const int firstSeparator = leftCount;
    const int secondSeparator = leftCount + 1 + middleCount;

    // Создание новой (средней) страницы и распределение элементов
    ProtoBStarPage middle = makePage(header, child.is_leaf());
    std::vector<ProtoIndexEntry> newLeftEntries(combined.begin(), combined.begin() + leftCount);
    std::vector<ProtoIndexEntry> middleEntries(combined.begin() + firstSeparator + 1, combined.begin() + secondSeparator);
    std::vector<ProtoIndexEntry> newChildEntries(combined.begin() + secondSeparator + 1, combined.end());

    replaceEntries(left, newLeftEntries);
    replaceEntries(middle, middleEntries);
    replaceEntries(child, newChildEntries);

    // Распределение дочерних указателей между тремя новыми узлами
    if (!child.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> leftChildren = pageChildren(left);
        std::vector<long long> childChildren = pageChildren(child);
        combinedChildren.insert(combinedChildren.end(), leftChildren.begin(), leftChildren.end());
        combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());

        std::vector<long long> newLeftChildren(combinedChildren.begin(), combinedChildren.begin() + leftCount + 1);
        std::vector<long long> middleChildren(combinedChildren.begin() + leftCount + 1, combinedChildren.begin() + leftCount + 1 + middleCount + 1);
        std::vector<long long> newChildChildren(combinedChildren.begin() + leftCount + 1 + middleCount + 1, combinedChildren.end());
        replaceChildren(left, newLeftChildren);
        replaceChildren(middle, middleChildren);
        replaceChildren(child, newChildChildren);
    }

    // Инъекция двух новых разделителей и ссылки на middle-страницу в родительский узел
    std::vector<ProtoIndexEntry> parentEntries = pageEntries(parent);
    std::vector<long long> parentChildren = pageChildren(parent);
    parentEntries[childIndex - 1] = combined[firstSeparator];
    parentEntries.insert(parentEntries.begin() + childIndex, combined[secondSeparator]);
    parentChildren.insert(parentChildren.begin() + childIndex, middle.page_id());
    replaceEntries(parent, parentEntries);
    replaceChildren(parent, parentChildren);

    writePage(header, left);
    writePage(header, middle);
    writePage(header, child);
    return true;
}

// Сплит B*-дерева: превращает 2 переполненных узла (текущий + правый) в 3 заполненных на 2/3
bool DiskBStarIndex::splitTwoToThreeWithRight(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex + 1 >= parent.child_page_ids_size())
    {
        return false;
    }

    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    ProtoBStarPage right = readPage(header, parent.child_page_ids(childIndex + 1));
    if (right.is_leaf() != child.is_leaf() || right.entries_size() < kMaxEntries || child.entries_size() < kMaxEntries)
    {
        return false;
    }

    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    std::vector<ProtoIndexEntry> rightEntries = pageEntries(right);
    combined.insert(combined.end(), childEntries.begin(), childEntries.end());
    combined.push_back(parent.entries(childIndex));
    combined.insert(combined.end(), rightEntries.begin(), rightEntries.end());

    // Расчет пропорций деления на 3 части и 2 разделителя
    const int total = static_cast<int>(combined.size());
    const int remaining = total - 2;
    const int leftCount = remaining / 3;
    const int middleCount = remaining / 3;
    const int firstSeparator = leftCount;
    const int secondSeparator = leftCount + 1 + middleCount;

    // Создание новой (средней) страницы и распределение элементов
    ProtoBStarPage middle = makePage(header, child.is_leaf());
    std::vector<ProtoIndexEntry> newChildEntries(combined.begin(), combined.begin() + leftCount);
    std::vector<ProtoIndexEntry> middleEntries(combined.begin() + firstSeparator + 1, combined.begin() + secondSeparator);
    std::vector<ProtoIndexEntry> newRightEntries(combined.begin() + secondSeparator + 1, combined.end());

    replaceEntries(child, newChildEntries);
    replaceEntries(middle, middleEntries);
    replaceEntries(right, newRightEntries);

    // Распределение дочерних указателей между тремя новыми узлами
    if (!child.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> childChildren = pageChildren(child);
        std::vector<long long> rightChildren = pageChildren(right);
        combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());
        combinedChildren.insert(combinedChildren.end(), rightChildren.begin(), rightChildren.end());

        std::vector<long long> newChildChildren(combinedChildren.begin(), combinedChildren.begin() + leftCount + 1);
        std::vector<long long> middleChildren(combinedChildren.begin() + leftCount + 1, combinedChildren.begin() + leftCount + 1 + middleCount + 1);
        std::vector<long long> newRightChildren(combinedChildren.begin() + leftCount + 1 + middleCount + 1, combinedChildren.end());
        replaceChildren(child, newChildChildren);
        replaceChildren(middle, middleChildren);
        replaceChildren(right, newRightChildren);
    }

    // Инъекция двух новых разделителей и ссылки на middle-страницу в родительский узел
    std::vector<ProtoIndexEntry> parentEntries = pageEntries(parent);
    std::vector<long long> parentChildren = pageChildren(parent);
    parentEntries[childIndex] = combined[firstSeparator];
    parentEntries.insert(parentEntries.begin() + childIndex + 1, combined[secondSeparator]);
    parentChildren.insert(parentChildren.begin() + childIndex + 1, middle.page_id());
    replaceEntries(parent, parentEntries);
    replaceChildren(parent, parentChildren);

    writePage(header, child);
    writePage(header, middle);
    writePage(header, right);
    return true;
}

// Подготавливает дочерний узел к вставке, балансируя его при переполнении
bool DiskBStarIndex::prepareChildForInsert(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    compactLeafTombstones(child); // Очистка удаленных записей перед проверкой размера
    writePage(header, child);

    if (child.entries_size() < kMaxEntries)
    {
        return false;
    }

    // Каскад стратегий балансировки B*-дерева (сначала перераспределение, потом сплит)
    if (redistributeWithLeftForInsert(header, parent, childIndex)) return true;
    if (redistributeWithRightForInsert(header, parent, childIndex)) return true;
    if (splitTwoToThreeWithLeft(header, parent, childIndex)) return true;
    if (splitTwoToThreeWithRight(header, parent, childIndex)) return true;

    // Классический сплит 1-в-2, если продвинутые B* стратегии не применимы
    splitChild(header, parent, childIndex);
    return true;
}








// Классический сплит 1-в-2: делит переполненный узел по медиане на два
void DiskBStarIndex::splitChild(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    std::vector<long long> childChildren = pageChildren(child);

    const int medianIndex = kMinDegree - 1;
    ProtoIndexEntry median = childEntries[medianIndex];

    ProtoBStarPage right = makePage(header, child.is_leaf());

    // Распределение элементов между левой и новой правой страницами
    std::vector<ProtoIndexEntry> leftEntries(childEntries.begin(), childEntries.begin() + medianIndex);
    std::vector<ProtoIndexEntry> rightEntries(childEntries.begin() + medianIndex + 1, childEntries.end());
    replaceEntries(child, leftEntries);
    replaceEntries(right, rightEntries);

    // Распределение дочерних указателей для внутренних узлов
    if (!child.is_leaf())
    {
        std::vector<long long> leftChildren(childChildren.begin(), childChildren.begin() + kMinDegree);
        std::vector<long long> rightChildren(childChildren.begin() + kMinDegree, childChildren.end());
        replaceChildren(child, leftChildren);
        replaceChildren(right, rightChildren);
    }

    // Вставка медианы и ссылки на новую страницу в родительский узел
    std::vector<ProtoIndexEntry> parentEntries = pageEntries(parent);
    std::vector<long long> parentChildren = pageChildren(parent);
    parentEntries.insert(parentEntries.begin() + childIndex, median);
    parentChildren.insert(parentChildren.begin() + childIndex + 1, right.page_id());
    replaceEntries(parent, parentEntries);
    replaceChildren(parent, parentChildren);

    writePage(header, child);
    writePage(header, right);
}

// Балансировка после удаления: заимствует/перераспределяет элементы из левого соседа
bool DiskBStarIndex::borrowFromLeftAfterErase(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex <= 0)
    {
        return false;
    }

    ProtoBStarPage left = readPage(header, parent.child_page_ids(childIndex - 1));
    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    compactLeafTombstones(left);
    compactLeafTombstones(child);

    if (left.is_leaf() != child.is_leaf() || left.entries_size() <= kMinEntries)
    {
        return false;
    }

    // Объединение элементов левого соседа, разделителя и текущего узла
    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> leftEntries = pageEntries(left);
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    combined.insert(combined.end(), leftEntries.begin(), leftEntries.end());
    combined.push_back(parent.entries(childIndex - 1));
    combined.insert(combined.end(), childEntries.begin(), childEntries.end());

    const int remaining = static_cast<int>(combined.size()) - 1;
    const int newLeftCount = remaining / 2;
    const int separatorIndex = newLeftCount;

    // Выравнивание количества элементов и обновление родителя
    replaceEntries(left, std::vector<ProtoIndexEntry>(combined.begin(), combined.begin() + newLeftCount));
    replaceEntries(child, std::vector<ProtoIndexEntry>(combined.begin() + separatorIndex + 1, combined.end()));
    *parent.mutable_entries(childIndex - 1) = combined[separatorIndex];

    // Перенос дочерних указателей для внутренних узлов
    if (!child.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> leftChildren = pageChildren(left);
        std::vector<long long> childChildren = pageChildren(child);
        combinedChildren.insert(combinedChildren.end(), leftChildren.begin(), leftChildren.end());
        combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());
        replaceChildren(left, std::vector<long long>(combinedChildren.begin(), combinedChildren.begin() + newLeftCount + 1));
        replaceChildren(child, std::vector<long long>(combinedChildren.begin() + newLeftCount + 1, combinedChildren.end()));
    }

    writePage(header, left);
    writePage(header, child);
    return true;
}

// Балансировка после удаления: заимствует/перераспределяет элементы из правого соседа
bool DiskBStarIndex::borrowFromRightAfterErase(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex + 1 >= parent.child_page_ids_size())
    {
        return false;
    }

    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    ProtoBStarPage right = readPage(header, parent.child_page_ids(childIndex + 1));
    compactLeafTombstones(child);
    compactLeafTombstones(right);

    if (right.is_leaf() != child.is_leaf() || right.entries_size() <= kMinEntries)
    {
        return false;
    }

    // Объединение элементов текущего узла, разделителя и правого соседа
    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
    std::vector<ProtoIndexEntry> rightEntries = pageEntries(right);
    combined.insert(combined.end(), childEntries.begin(), childEntries.end());
    combined.push_back(parent.entries(childIndex));
    combined.insert(combined.end(), rightEntries.begin(), rightEntries.end());

    const int remaining = static_cast<int>(combined.size()) - 1;
    const int newChildCount = remaining / 2;
    const int separatorIndex = newChildCount;

    // Выравнивание количества элементов и обновление родителя
    replaceEntries(child, std::vector<ProtoIndexEntry>(combined.begin(), combined.begin() + newChildCount));
    replaceEntries(right, std::vector<ProtoIndexEntry>(combined.begin() + separatorIndex + 1, combined.end()));
    *parent.mutable_entries(childIndex) = combined[separatorIndex];

    // Перенос дочерних указателей для внутренних узлов
    if (!child.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> childChildren = pageChildren(child);
        std::vector<long long> rightChildren = pageChildren(right);
        combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());
        combinedChildren.insert(combinedChildren.end(), rightChildren.begin(), rightChildren.end());
        replaceChildren(child, std::vector<long long>(combinedChildren.begin(), combinedChildren.begin() + newChildCount + 1));
        replaceChildren(right, std::vector<long long>(combinedChildren.begin() + newChildCount + 1, combinedChildren.end()));
    }

    writePage(header, child);
    writePage(header, right);
    return true;
}

// Слияние B*-дерева: объединяет 3 полупустых узла в 2 заполненных
bool DiskBStarIndex::mergeThreeToTwoAfterErase(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex <= 0 || childIndex + 1 >= parent.child_page_ids_size())
    {
        return false;
    }

    ProtoBStarPage left = readPage(header, parent.child_page_ids(childIndex - 1));
    ProtoBStarPage middle = readPage(header, parent.child_page_ids(childIndex));
    ProtoBStarPage right = readPage(header, parent.child_page_ids(childIndex + 1));
    compactLeafTombstones(left);
    compactLeafTombstones(middle);
    compactLeafTombstones(right);

    if (left.is_leaf() != middle.is_leaf() || right.is_leaf() != middle.is_leaf())
    {
        return false;
    }

    // Сборка всех элементов из трех узлов и двух родительских разделителей
    std::vector<ProtoIndexEntry> combined;
    std::vector<ProtoIndexEntry> leftEntries = pageEntries(left);
    std::vector<ProtoIndexEntry> middleEntries = pageEntries(middle);
    std::vector<ProtoIndexEntry> rightEntries = pageEntries(right);
    combined.insert(combined.end(), leftEntries.begin(), leftEntries.end());
    combined.push_back(parent.entries(childIndex - 1));
    combined.insert(combined.end(), middleEntries.begin(), middleEntries.end());
    combined.push_back(parent.entries(childIndex));
    combined.insert(combined.end(), rightEntries.begin(), rightEntries.end());

    if (combined.size() > static_cast<std::size_t>(2 * kMaxEntries + 1))
    {
        return false; // Слишком много элементов для двух страниц
    }

    const int remaining = static_cast<int>(combined.size()) - 1;
    const int newLeftCount = remaining / 2;
    const int separatorIndex = newLeftCount;

    // Перераспределение данных в left и right страницы (middle ликвидируется)
    replaceEntries(left, std::vector<ProtoIndexEntry>(combined.begin(), combined.begin() + newLeftCount));
    replaceEntries(right, std::vector<ProtoIndexEntry>(combined.begin() + separatorIndex + 1, combined.end()));

    // Перенос всех дочерних указателей
    if (!middle.is_leaf())
    {
        std::vector<long long> combinedChildren;
        std::vector<long long> leftChildren = pageChildren(left);
        std::vector<long long> middleChildren = pageChildren(middle);
        std::vector<long long> rightChildren = pageChildren(right);
        combinedChildren.insert(combinedChildren.end(), leftChildren.begin(), leftChildren.end());
        combinedChildren.insert(combinedChildren.end(), middleChildren.begin(), middleChildren.end());
        combinedChildren.insert(combinedChildren.end(), rightChildren.begin(), rightChildren.end());
        replaceChildren(left, std::vector<long long>(combinedChildren.begin(), combinedChildren.begin() + newLeftCount + 1));
        replaceChildren(right, std::vector<long long>(combinedChildren.begin() + newLeftCount + 1, combinedChildren.end()));
    }

    // Корректировка родительского узла: один разделитель обновляется, второй удаляется
    std::vector<ProtoIndexEntry> parentEntries = pageEntries(parent);
    std::vector<long long> parentChildren = pageChildren(parent);
    parentEntries[childIndex - 1] = combined[separatorIndex];
    parentEntries.erase(parentEntries.begin() + childIndex);
    parentChildren.erase(parentChildren.begin() + childIndex);
    replaceEntries(parent, parentEntries);
    replaceChildren(parent, parentChildren);

    writePage(header, left);
    writePage(header, right);
    freeOldPageSlot(header, middle.page_id()); // Освобождение дискового пространства удаленного узла
    return true;
}

// Классическое слияние 2-в-1: объединяет текущую страницу с левым или правым соседом
bool DiskBStarIndex::mergeTwoToOneAfterErase(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    std::vector<ProtoIndexEntry> parentEntries = pageEntries(parent);
    std::vector<long long> parentChildren = pageChildren(parent);

    // Случай А: Слияние с левым соседом
    if (childIndex > 0)
    {
        ProtoBStarPage left = readPage(header, parent.child_page_ids(childIndex - 1));
        ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
        compactLeafTombstones(left);
        compactLeafTombstones(child);
        if (left.is_leaf() != child.is_leaf()) return false;

        std::vector<ProtoIndexEntry> combined;
        std::vector<ProtoIndexEntry> leftEntries = pageEntries(left);
        std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
        combined.insert(combined.end(), leftEntries.begin(), leftEntries.end());
        combined.push_back(parent.entries(childIndex - 1));
        combined.insert(combined.end(), childEntries.begin(), childEntries.end());
        if (combined.size() > static_cast<std::size_t>(kMaxEntries)) return false;
        replaceEntries(left, combined);

        if (!child.is_leaf())
        {
            std::vector<long long> combinedChildren;
            std::vector<long long> leftChildren = pageChildren(left);
            std::vector<long long> childChildren = pageChildren(child);
            combinedChildren.insert(combinedChildren.end(), leftChildren.begin(), leftChildren.end());
            combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());
            replaceChildren(left, combinedChildren);
        }

        parentEntries.erase(parentEntries.begin() + childIndex - 1);
        parentChildren.erase(parentChildren.begin() + childIndex);
        replaceEntries(parent, parentEntries);
        replaceChildren(parent, parentChildren);
        writePage(header, left);
        freeOldPageSlot(header, child.page_id());
        return true;
    }

    // Случай Б: Слияние с правым соседом
    if (childIndex + 1 < parent.child_page_ids_size())
    {
        ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
        ProtoBStarPage right = readPage(header, parent.child_page_ids(childIndex + 1));
        compactLeafTombstones(child);
        compactLeafTombstones(right);
        if (child.is_leaf() != right.is_leaf()) return false;

        std::vector<ProtoIndexEntry> combined;
        std::vector<ProtoIndexEntry> childEntries = pageEntries(child);
        std::vector<ProtoIndexEntry> rightEntries = pageEntries(right);
        combined.insert(combined.end(), childEntries.begin(), childEntries.end());
        combined.push_back(parent.entries(childIndex));
        combined.insert(combined.end(), rightEntries.begin(), rightEntries.end());
        if (combined.size() > static_cast<std::size_t>(kMaxEntries)) return false;
        replaceEntries(child, combined);

        if (!child.is_leaf())
        {
            std::vector<long long> combinedChildren;
            std::vector<long long> childChildren = pageChildren(child);
            std::vector<long long> rightChildren = pageChildren(right);
            combinedChildren.insert(combinedChildren.end(), childChildren.begin(), childChildren.end());
            combinedChildren.insert(combinedChildren.end(), rightChildren.begin(), rightChildren.end());
            replaceChildren(child, combinedChildren);
        }

        parentEntries.erase(parentEntries.begin() + childIndex);
        parentChildren.erase(parentChildren.begin() + childIndex + 1);
        replaceEntries(parent, parentEntries);
        replaceChildren(parent, parentChildren);
        writePage(header, child);
        freeOldPageSlot(header, right.page_id());
        return true;
    }

    return false;
}

// Восстанавливает минимальное заполнение дочернего узла после удаления элемента
void DiskBStarIndex::repairChildAfterErase(HeaderData& header, ProtoBStarPage& parent, int childIndex)
{
    if (childIndex < 0 || childIndex >= parent.child_page_ids_size())
    {
        return;
    }

    ProtoBStarPage child = readPage(header, parent.child_page_ids(childIndex));
    compactLeafTombstones(child);
    writePage(header, child);
    if (child.entries_size() >= kMinEntries)
    {
        return; // Узел самодостаточен, балансировка не нужна
    }

    // Каскад стратегий восстановления (заимствование -> слияние 3-в-2 -> слияние 2-в-1)
    if (borrowFromLeftAfterErase(header, parent, childIndex)) return;
    if (borrowFromRightAfterErase(header, parent, childIndex)) return;
    if (mergeThreeToTwoAfterErase(header, parent, childIndex)) return;
    mergeTwoToOneAfterErase(header, parent, childIndex);
}

// Корректирует или ликвидирует корень дерева, если он опустел после удаления
void DiskBStarIndex::fixRootAfterErase(HeaderData& header)
{
    if (!header.rootExists)
    {
        return;
    }

    ProtoBStarPage root = readPage(header, header.rootPageId);
    compactLeafTombstones(root);

    // Корень является листом
    if (root.is_leaf())
    {
        if (root.entries_size() == 0)
        {
            freeOldPageSlot(header, root.page_id());
            header.rootExists = false;
            header.rootPageId = -1;
            header.nextPageId = header.nextPageId;
        }
        else
        {
            writePage(header, root);
        }
        return;
    }

    // Внутренний корень опустел — его единственный ребенок становится новым корнем
    if (root.entries_size() == 0 && root.child_page_ids_size() == 1)
    {
        const long long newRoot = root.child_page_ids(0);
        freeOldPageSlot(header, root.page_id());
        header.rootPageId = newRoot;
        header.rootExists = true;
        return;
    }

    writePage(header, root);
}

// Рекурсивная вставка элемента в узел, гарантированно защищенный от переполнения
bool DiskBStarIndex::insertNonFull(HeaderData& header, long long pageId, const Value& key, std::streamoff offset)
{
    ProtoBStarPage page = readPage(header, pageId);
    int position = findEntryPosition(page, key);

    // Если ключ уже есть и он помечен как удаленный — реанимируем запись
    if (position < page.entries_size() && compareKeyWithEntry(key, page.entries(position)) == 0)
    {
        if (!page.entries(position).deleted())
        {
            return false;
        }
        *page.mutable_entries(position) = makeEntry(key, offset, false);
        writePage(header, page);
        return true;
    }

    // Вставка непосредственно в лист
    if (page.is_leaf())
    {
        compactLeafTombstones(page);
        position = findEntryPosition(page, key);
        std::vector<ProtoIndexEntry> entries = pageEntries(page);
        entries.insert(entries.begin() + position, makeEntry(key, offset, false));
        replaceEntries(page, entries);
        writePage(header, page);
        return true;
    }

    // Продвижение вниз: превентивно балансируем ребенка перед спуском
    prepareChildForInsert(header, page, position);
    writePage(header, page);
    page = readPage(header, pageId);
    position = findEntryPosition(page, key);

    // Проверка: не стал ли ключ разделителем после балансировки
    if (position < page.entries_size() && compareKeyWithEntry(key, page.entries(position)) == 0)
    {
        if (!page.entries(position).deleted())
        {
            return false;
        }
        *page.mutable_entries(position) = makeEntry(key, offset, false);
        writePage(header, page);
        return true;
    }

    if (position >= page.child_page_ids_size())
    {
        throw std::runtime_error("повреждена структура файлового B*-индекса: нет child после балансировки");
    }

    // Рекурсивный спуск в подготовленный дочерний узел
    return insertNonFull(header, page.child_page_ids(position), key, offset);
}

// Публичный метод вставки: обрабатывает создание корня, сплит корня и запуск вставки
bool DiskBStarIndex::insert(const Value& key, std::streamoff offset)
{
    if (key.type == ValueType::Null || !valueHasColumnType(key, keyType_))
    {
        throw std::runtime_error("ключ не соответствует типу файлового B*-индекса");
    }

    HeaderData header = loadHeader();
    if (header.rootExists && contains(key))
    {
        return false; // Дубликаты запрещены
    }

    // Дерево пустое: инициализация первого корневого листа
    if (!header.rootExists)
    {
        ProtoBStarPage root = makePage(header, true);
        *root.add_entries() = makeEntry(key, offset, false);
        writePage(header, root);
        header.rootExists = true;
        header.rootPageId = root.page_id();
        header.size = 1;
        saveHeader(header);
        return true;
    }

    // Если корень переполнен, увеличиваем высоту дерева (классический сплит корня)
    ProtoBStarPage root = readPage(header, header.rootPageId);
    compactLeafTombstones(root);
    if (root.entries_size() >= kMaxEntries)
    {
        ProtoBStarPage newRoot = makePage(header, false);
        newRoot.add_child_page_ids(root.page_id());
        splitChild(header, newRoot, 0);
        writePage(header, newRoot);
        header.rootPageId = newRoot.page_id();
    }

    const bool inserted = insertNonFull(header, header.rootPageId, key, offset);
    if (inserted)
    {
        header.size += 1;
        saveHeader(header);
    }
    return inserted;
}

// Рекурсивное удаление элемента из страницы (физическое в листах, логическое во внутренних)
bool DiskBStarIndex::eraseInPage(HeaderData& header, long long pageId, const Value& key)
{
    ProtoBStarPage page = readPage(header, pageId);
    const int position = findEntryPosition(page, key);

    // Ключ найден на текущей странице
    if (position < page.entries_size() && compareKeyWithEntry(key, page.entries(position)) == 0)
    {
        if (page.entries(position).deleted())
        {
            return false;
        }

        // Из листа удаляем физически
        if (page.is_leaf())
        {
            std::vector<ProtoIndexEntry> entries = pageEntries(page);
            entries.erase(entries.begin() + position);
            replaceEntries(page, entries);
            writePage(header, page);
            return true;
        }

        // Из внутреннего узла удаляем логически (выставляем tombstone)
        ProtoIndexEntry entry = page.entries(position);
        entry.set_deleted(true);
        entry.set_offset(0);
        *page.mutable_entries(position) = entry;
        writePage(header, page);
        return true;
    }

    if (page.is_leaf())
    {
        return false;
    }

    if (position >= page.child_page_ids_size())
    {
        return false;
    }

    // Рекурсивный спуск с последующим восстановлением инварианта дерева на обратном пути
    const bool erased = eraseInPage(header, page.child_page_ids(position), key);
    if (erased)
    {
        page = readPage(header, pageId);
        repairChildAfterErase(header, page, position);
        writePage(header, page);
    }
    return erased;
}

// Публичный метод удаления: запускает процесс и обновляет метаданные
bool DiskBStarIndex::erase(const Value& key)
{
    HeaderData header = loadHeader();
    if (!header.rootExists)
    {
        return false;
    }

    const bool erased = eraseInPage(header, header.rootPageId, key);
    if (erased)
    {
        header.size -= 1;
        if (header.size < 0)
        {
            header.size = 0;
        }

        fixRootAfterErase(header);
        saveHeader(header);
    }
    return erased;
}

// Рекурсивный обход дерева (In-Order) для сбора всех активных записей в один список
void DiskBStarIndex::collectInOrder(const HeaderData& header, long long pageId, std::vector<ProtoIndexEntry>& entries) const
{
    ProtoBStarPage page = readPage(header, pageId);

    if (page.is_leaf())
    {
        for (int index = 0; index < page.entries_size(); ++index)
        {
            if (!page.entries(index).deleted())
            {
                entries.push_back(page.entries(index));
            }
        }
        return;
    }

    for (int index = 0; index < page.entries_size(); ++index)
    {
        if (index < page.child_page_ids_size())
        {
            collectInOrder(header, page.child_page_ids(index), entries);
        }
        if (!page.entries(index).deleted())
        {
            entries.push_back(page.entries(index));
        }
    }

    if (page.child_page_ids_size() > page.entries_size())
    {
        collectInOrder(header, page.child_page_ids(page.entries_size()), entries);
    }
}

// Фильтрует плоский список записей по заданному диапазону (Low, High) и собирает смещения
std::vector<std::streamoff> DiskBStarIndex::filterOffsets(
    const std::vector<ProtoIndexEntry>& entries,
    const Value& low,
    bool hasLow,
    const Value& high,
    bool hasHigh,
    bool includeLow,
    bool includeHigh
) const
{
    std::vector<std::streamoff> offsets;
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        Value key = entryKey(entries[index]);
        bool ok = true;
        if (hasLow)
        {
            int cmp = compareValues(key, low);
            ok = ok && (includeLow ? cmp >= 0 : cmp > 0);
        }
        if (hasHigh)
        {
            int cmp = compareValues(key, high);
            ok = ok && (includeHigh ? cmp <= 0 : cmp < 0);
        }
        if (ok)
        {
            offsets.push_back(static_cast<std::streamoff>(entries[index].offset()));
        }
    }
    return offsets;
}

// Возвращает смещения для всех ключей, которые меньше заданного (ограничение сверху)
std::vector<std::streamoff> DiskBStarIndex::lessThan(const Value& key, bool inclusive) const
{
    HeaderData header = loadHeader();
    std::vector<ProtoIndexEntry> entries;
    if (header.rootExists)
    {
        collectInOrder(header, header.rootPageId, entries);
    }
    Value dummy = makeNull();
    return filterOffsets(entries, dummy, false, key, true, false, inclusive);
}

// Возвращает смещения для всех ключей, которые больше заданного (ограничение снизу)
std::vector<std::streamoff> DiskBStarIndex::greaterThan(const Value& key, bool inclusive) const
{
    HeaderData header = loadHeader();
    std::vector<ProtoIndexEntry> entries;
    if (header.rootExists)
    {
        collectInOrder(header, header.rootPageId, entries);
    }
    Value dummy = makeNull();
    return filterOffsets(entries, key, true, dummy, false, inclusive, false);
}

// Возвращает смещения для ключей, находящихся внутри замкнутого диапазона [low, high]
std::vector<std::streamoff> DiskBStarIndex::between(const Value& low, const Value& high) const
{
    HeaderData header = loadHeader();
    std::vector<ProtoIndexEntry> entries;
    if (header.rootExists)
    {
        collectInOrder(header, header.rootPageId, entries);
    }
    return filterOffsets(entries, low, true, high, true, true, true);
}

// Экспортирует базовые метаданные заголовка в строку Protobuf
std::string DiskBStarIndex::exportTreeToProtoBytes() const
{
    HeaderData header = loadHeader();
    ProtoBStarDiskHeader proto;
    proto.set_root_exists(header.rootExists);
    proto.set_root_page_id(header.rootPageId);
    proto.set_next_page_id(header.nextPageId);
    proto.set_size(header.size);
    std::string bytes;
    proto.SerializeToString(&bytes);
    return bytes;
}

// Заглушка для импорта дерева из Protobuf строки
void DiskBStarIndex::importTreeFromProtoBytes(const std::string& bytes)
{
    (void)bytes;
}