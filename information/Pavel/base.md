# Файл с ответами на основные вопросы

## 1. Синтаксис языка запросов

### 1. Команды могут занимать несколько строк и завершаются символом ;
```
utils.cpp / splitStatements()
```

### 2. Ключевые слова регистронезависимы (без смешения регистров в одном слове)
```
utils.cpp / toUpper()
lexer.cpp / consumeIf(), expectWords()
parser.cpp / все проверки ключевых слов
```

### 3. Строковые литералы заключаются в двойные кавычки (")
```
lexer.cpp / Lexer::Lexer()
```

### 4. Правила имён сущностей (БД, таблиц, колонок)
```
utils.cpp / isValidIdentifier()
```

### 5. Обращение к таблицам: database_name.table_name или через USE
```
Реализация: parser.cpp Parser::parseTableName()
Использование: dbms.cpp 
        resolveDataBaseName() - выбирает способ открытия таблицы
        requireDataBaseFromTableName() - открывает нужную базу данных

USE реализовано в dbms.cpp / DBMS::executeUseDatabase()
```

**Общая схема обработки SQL-текста**
```
Многострочный текст
    ↓
splitStatements()                  // utils.cpp
    ↓
Lexer (токенизация)                // lexer.cpp
    ↓
Parser::parseStatement()           // parser.cpp
    ↓
DBMS::execute()                    // dbms.cpp
    ↓
Table / Database / Auth и т.д.
```

## 2. Работа с метаданными СУБД

### 1. Парсинг команд

```
parser.cpp / Parser::parseStatement()
```

### 2. Диспетчеризация выполнения
```
dbms.cpp / DBMS::execute()
```

### 3. Основная реализация
```
dbms.cpp / DBMS::execute<название команды>
```

### 4. Вспомогательные алгоритмы
```
dbms.cpp /
    databasePath() - формирует путь
    resolveDataname() - определяет, какую базу использовать
    requireDataFromTableName() - открывает базу данных для операций с таблицами
    metadataMutex() - глобальный мьютекс
```

### 5. Поток выполнения (пример)
```
CREATE DATABASE testdb;
SQL → Lexer → Parser::parseStatement() → CreateDatabaseCommand
    → DBMS::execute() → executeCreateDatabase()
        → metadataMutex()
        → create_directories("data/testdb")
        → "OK: база данных создана: testdb"

USE testdb;
→ Parser → UseDatabaseCommand
    → DBMS::executeUseDatabase()
        → currentDatabase_ = "testdb"

DROP DATABASE testdb;
→ Parser → DropDatabaseCommand
    → DBMS::executeDropDatabase()
        → remove_all("data/testdb")
        → если нужно — сброс currentDatabase_
```

## 3. Работа со схемами данных (DDL)

### 1. Парсинг команд
```
CREATE TABLE parser.cpp / Parser::parserCreateTable()
DROP TABLE parser.cpp / Parse::parseStatement()
```

### 2. Диспетчеризация выполнения
```
dbms.cpp / DBMS::execute()
```

### 3. Основная реализация
```
table.cpp 
CREATE_TABLE: Table::create()