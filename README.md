[![Build Status](https://travis-ci.org/tarantool/mysql-tarantool-replication.svg?branch=master)](https://travis-ci.org/tarantool/mysql-tarantool-replication)

# MySQL slave replication daemon for Tarantool
----------------------------------------------
Key features:
* Listens to a MySQL server as a replication client and translates incoming
replication events into Tarantool space operations.
* Handles a mapping between MySQL tables and Tarantool spaces.

>**Note**:
MySQL binary log should be in the row-based format.
Will not work with a MariaDB master.
Tested with MySQL 5.6 and lower.
Not tested with MySQL 5.7 with new replication protocol improvements.

[About Tarantool](http://tarantool.org)

## <a name="contents"></a>Contents
----------
* [Compilation and installation](#compilation-and-installation)
* [MySQL configuration](#mysql-configuration)
  * [Build from source](#build-from-source)
* [Tarantool configuration](#tarantool-configuration)
* [Replicator configuration](#replicator-configuration)
  * [Mappings](#mappings)

## <a name="compilation-and-installation"></a>Compilation and installation
--------------------------

### <a name="build-from-source"></a>Build from source

 ```
 bash
 git clone https://github.com/tarantool/mysql-tarantool-replication.git mysql_tarantool-replication
 cd mysql-tarantool-replication
 git submodule update --init --recursive
 cmake .
 make
 ```

[Back to contents](#contents)

## <a name="mysql-configuration"></a>MySQL configuration
---------------------

1. Set the `binlog_format` to `ROW`.<br>
That can be done by editing the MySQL configuration file at `/etc/mysql/my.cnf`:
 ```
 binlog_format = ROW
 ```

2. Create a user for replication, for example:
 ```
 CREATE USER <username>@'<host>' IDENTIFIED BY '<password>';
 ```

3. Grant replication privileges to the new user:
 ```
 GRANT REPLICATION CLIENT ON '<db>'.'<table>' TO <username>@'<domain>';
 GRANT REPLICATION SLAVE ON '<db>'.'<table>' TO <username>@'<domain>';
 GRANT SELECT ON '<db>'.'<table>' TO <username>@'<domain>';
 ```
>**Note**: you can use an asterisk (\*) as a wildcard instead of specifying a database and a table.

4. Flush the changes:
 ```
 FLUSH PRIVILEGES
 ```

[Back to contents](#contents)

## <a name="tarantool-configuration"></a>Tarantool configuration
--------------------------

1. Create one space for replication daemon purposes.<br>
This space will store the current replication state.

2. Create more spaces to store replicated data.

3. Create a user for the replication daemon and enable read/write operations on
target spaces.

[Back to contents](#contents)

## <a name="replicator-configuration"></a>Replicator configuration
---------------------------

1. Set up MySQL connection parameters: `host`, `port`, `login` and `password`.

2. Set up Tarantool connection options: `host`, `port`, `login` and `password`.

3. Set `binlog_pos_space` to the space id created for the replication state and
`binlog_pos_key` to identify the corresponding tuple in a given space.

### <a name="mappings"></a>Mappings
------------

Replicator can map MySQL tables to one or more Tarantool spaces.
Each mapping item contains the names of a database and a table, a list of replicated columns,
a space id (if the space id is the same as for the previous item, it is left blank) and space
key fields numbers.

Multiple MySQL tables can be mapped to one Tarantool space, and the first
table in the mapping is primary. In this case, each subsequent mapping item adds
its columns to the end of the list of replicated columns for the space. When
a non-primary table deletes its row, the corresponding fields in the tuple are
reset to `NULL`. When a row is inserted into a non-primary table, the
corresponding tuple is updated if it already exists or inserted if not, and all the fields
before the field mapping are set to `NULL`.

>**Note**: the configuration file contains a **Spaces** section, where you can set default values
for tuples - for example, for indexed fields.

[Back to contents](#contents)
