CREATE DATABASE demo;
USE demo;
DROP TABLE demo.users;
CREATE TABLE users (
    id INT,
    name STRING,
    city STRING ,
    age INT
);

INSERT INTOq users (id, name, city, age) VALUES
    (1, "Алиса", "Берлин", 21),
    (2, "Борис", "Париж", 19),
    (3, "Клара", "Рим", 30),
    (4, "Даниил", "Берлин", 17);

SELECT * FROM users;
