# Mysql slave replication daemon for tarantool
----------------------------------------------
Key features:
* Listens a mysql server as a replication client and translates incomming
replication events into tarantool space operations.
* Handles a mapping between mysql tables and tarantool spaces.

Note:
Mysql binlog should be configured as row-based.
Will not work with mariadb master.
Tested with Mysql 5.6 and below.
Not tested with MySQL 5.7 with new replication protocol improvements.

About Tarantool: http://tarantool.org

## Content
----------
* [Compilation and install](#compilation-and-install)
* [Mysql configuration](#mysql-configuration)
* [Tarantool configuration](#tarantool-configuration)
* [Replicator configuration](#replicator configuration)
  * [Mappings](#mappings)

## Compilation and install
--------------------------

### Build from source
```bash
git clone https://github.com/tarantool/mysql-tarantool-replication.git mysql_tarantool-replication
cd mysql-tarantool-replication
git submodule update --init --recursive
cmake .
make
```

[Back to content](#content)

## Mysql configuraion
---------------------

Set mysql binary log format to row. That can be done with editing mysql
configuration file (default: /etc/mysql/my.cnf):
```binlog_format = ROW
```

Create user for replication, for example:
```CREATE USER <username>@'<host>' IDENTIFIED BY '<password>';
```

Grant replication priveleges to new user:
```GRANT REPLICATION CLIENT ON '<db>'.'<table>' TO <username>@'<domain>';
GRANT REPLICATION SLAVE ON '<db>'.'<table>' TO <username>@'<domain>';
GRANT SELECT ON '<db>'.'<table>' TO <username>@'<domain>';

```
Note: you can use `*' wildcard instead of db and table specification.

Flush changes:
```FLUSH PRIVILEGES
```

[Back to content](#content)

## Tarantool configuration
--------------------------

Create one space for replication daemon purposes, this space will store
current replication state. Create more spaces to store replicated data.

Create user for replication daemon and enable read/write operation on
target spaces.

[Back to content](#content)

## Replicator configuration
---------------------------

Setup mysql connection params: host, port, login and password.

Setup tarantool connection options: host, port, login and password.
Set binlog_pos_space to space id created for replication state and
binlog_pos_key to identify corresponding tuple in given space.

### Mappings
------------

Replicator can map mysql tables to one or more tarantool spaces.
Each mapping item contains database and table name, replicated column list,
space id (or empty, if space id is same as for previous item) and space
key fields numbers.
Multiple mysql tables can be mapped into one tarantool space and first
table in mapping will be primary. In this case each next mapping item adds
its columns to end of list of replicated column for the space. When
non-primary table deletes its row then corresponding fields in tuple will
be reset to null. When a row was inserted into non-primary table then
corresponding tuple will be updated if exists or inserted, and all fields
before field mapping will be set to null.

Note: config contains a 'spaces' section. With this section default values
for tuples can be set, for example for indexed fields.

[Back to content](#content)

