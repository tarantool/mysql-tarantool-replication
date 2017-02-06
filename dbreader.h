#ifndef REPLICATOR_DBREADER_H
#define REPLICATOR_DBREADER_H

#include <vector>
#include <string>
#include <utility>
#include <functional>

#include <Slave.h>
#include <DefaultExtState.h>
#include <nanomysql.h>

#include "serializable.h"

namespace replicator {

typedef std::function<bool (SerializableBinlogEventPtr&&)> BinlogEventCallback;

struct DBTable
{
	DBTable(
		const std::string& db,
		const std::string& table,
		const std::map<std::string, std::pair<unsigned, bool>>& filter_,
		bool is_primary_
	) :
		name(db, table), filter(filter_), is_primary(is_primary_)
	{}

	const std::pair<std::string, std::string> name;
	const std::map<std::string, std::pair<unsigned, bool>> filter;
	const bool is_primary;
};

class DBReader
{
public:
	DBReader(nanomysql::mysql_conn_opts& opts, unsigned connect_retry)
		: state(), slave(slave::MasterInfo(opts, connect_retry), state), stopped(false) {}

	~DBReader() {
		slave.close_connection();
	}

	void AddTable(
		const std::string& db,
		const std::string& table,
		const std::map<std::string, std::pair<unsigned, bool>>& filter,
		bool is_primary
	) {
		tables.emplace_back(db, table, filter, is_primary);
	}

	void DumpTables(std::string& binlog_name, unsigned long& binlog_pos, const BinlogEventCallback& cb);
	void ReadBinlog(const std::string& binlog_name, unsigned long binlog_pos, const BinlogEventCallback& cb);
	void Stop();

	void EventCallbackNormal(
		const slave::RecordSet& event,
		const std::map<std::string, std::pair<unsigned, bool>>& filter,
		const BinlogEventCallback& cb
	);
	void EventCallbackNullify(
		const slave::RecordSet& event,
		const std::map<std::string, std::pair<unsigned, bool>>& filter,
		const BinlogEventCallback& cb
	);
	void XidEventCallback(unsigned int server_id, const BinlogEventCallback& cb);
	void DumpTablesCallback(
		const std::string& db_name,
		const std::string& tbl_name,
		const std::vector<std::pair<unsigned, slave::PtrField>>& filter,
		const nanomysql::fields_t& f,
		const BinlogEventCallback& cb
	);

	// unsigned GetSecondsBehindMaster() const;

private:
	slave::DefaultExtState state;
	slave::Slave slave;
	std::vector<DBTable> tables;
	bool stopped;

	// ::time_t last_event_when;
};

} // replicator

#endif // REPLICATOR_DBREADER_H
