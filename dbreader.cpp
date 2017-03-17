#include <sstream>
#include "dbreader.h"

using std::placeholders::_1;

namespace replicator {

void DBReader::DumpTables(std::string& binlog_name, unsigned long& binlog_pos, const BinlogEventCallback& cb)
{
	const static slave::callback dummycallback = [] (const slave::RecordSet& event) {};
	// start temp slave to read DB structure
	slave::Slave tempslave(slave.masterInfo(), state);
	for (auto i = tables.begin(), end = tables.end(); i != end; ++i) {
		tempslave.setCallback(i->name.first, i->name.second, dummycallback);
	}

	tempslave.init();
	tempslave.createDatabaseStructure();

	// last_event_when = ::time(NULL);

	slave::Slave::binlog_pos_t bp = tempslave.getLastBinlog();
	binlog_name = bp.first;
	binlog_pos = bp.second;

	state.setMasterLogNamePos(bp.first, bp.second);

	// dump tables
	nanomysql::Connection conn(slave.masterInfo().conn_options);
	conn.query("SET NAMES utf8");

	for (auto table = tables.begin(), end = tables.end(); !stopped && table != end; ++table) {
		// build field_name -> field_ptr map for filtered columns
		std::vector<std::pair<unsigned, slave::PtrField>> filter_;
		const auto rtable = tempslave.getRli().getTable(table->name);
		std::string s_fields;

		for (const auto ptr_field : rtable->fields) {
			const auto it = table->filter.find(ptr_field->field_name);
			if (it != table->filter.end()) {
				filter_.emplace_back(it->second.first, ptr_field);
				s_fields += '`';
				s_fields += ptr_field->field_name;
				s_fields += "`,";
			}
		}
		s_fields.pop_back();

		conn.select_db(table->name.first);
		conn.query(std::string("SELECT ") + s_fields  + " FROM " + table->name.second);
		conn.use(std::bind(&DBReader::DumpTablesCallback,
			this,
			std::cref(table->name.first),
			std::cref(table->name.second),
			std::cref(filter_),
			_1,
			std::cref(cb)
		));
	}

	// send binlog position update event
	if (!stopped) {
		SerializableBinlogEventPtr ev(new SerializableBinlogEvent);
		ev->binlog_name = binlog_name;
		ev->binlog_pos = binlog_pos;
		// ev->seconds_behind_master = GetSecondsBehindMaster();
		// ev->unix_timestamp = long(time(NULL));
		ev->event = "IGNORE";
		stopped = cb(std::move(ev));
	}

	//tempslave.close_connection();
}

void DBReader::ReadBinlog(const std::string& binlog_name, unsigned long binlog_pos, const BinlogEventCallback& cb)
{
	stopped = false;
	state.setMasterLogNamePos(binlog_name, binlog_pos);

	for (auto table = tables.begin(), end = tables.end(); table != end; ++table) {
		slave.setCallback(
			table->name.first,
			table->name.second,
			std::bind(
				table->is_primary
					? &DBReader::EventCallbackNormal
					: &DBReader::EventCallbackNullify,
				this, _1, std::cref(table->filter), std::cref(cb)
			)
		);
	}
	slave.setXidCallback(std::bind(&DBReader::XidEventCallback, this, _1, std::cref(cb)));
	slave.init();
	slave.createDatabaseStructure();

	slave.get_remote_binlog([this] { return stopped; });
}

void DBReader::Stop()
{
	stopped = true;
	slave.close_connection();
}

void DBReader::EventCallbackNormal(
	const slave::RecordSet& event,
	const std::map<std::string, std::pair<unsigned, bool>>& filter,
	const BinlogEventCallback& cb
) {
	if (stopped) return;
	// last_event_when = event.when;

	SerializableBinlogEventPtr ev(new SerializableBinlogEvent);

	switch (event.type_event) {
		case slave::RecordSet::Delete: ev->event = "DELETE"; break;
		case slave::RecordSet::Update: ev->event = "UPDATE"; break;
		case slave::RecordSet::Write:  ev->event = "INSERT"; break;
		default: return;
	}

	ev->binlog_name = state.getMasterLogName();
	ev->binlog_pos = state.getMasterLogPos();
	// ev->seconds_behind_master = GetSecondsBehindMaster();
	// ev->unix_timestamp = long(time(NULL));
	ev->database = event.db_name;
	ev->table = event.tbl_name;

	for (auto fi = filter.begin(), end = filter.end(); fi != end; ++fi) {
		const auto ri = event.m_row.find(fi->first);
		if (ri != event.m_row.end()) {
			ev->row[ fi->second.first ] = ri->second;
		}
	}
	stopped = cb(std::move(ev));
}

void DBReader::EventCallbackNullify(
	const slave::RecordSet& event,
	const std::map<std::string, std::pair<unsigned, bool>>& filter,
	const BinlogEventCallback& cb
) {
	if (stopped) return;
	// last_event_when = event.when;

	SerializableBinlogEventPtr ev(new SerializableBinlogEvent);
	bool is_delete = false;

	switch (event.type_event) {
		case slave::RecordSet::Delete: is_delete = true;
		case slave::RecordSet::Update: ev->event = "UPDATE"; break;
		case slave::RecordSet::Write:  ev->event = "INSERT"; break;
		default: return;
	}

	ev->binlog_name = state.getMasterLogName();
	ev->binlog_pos = state.getMasterLogPos();
	// ev->seconds_behind_master = GetSecondsBehindMaster();
	// ev->unix_timestamp = long(time(NULL));
	ev->database = event.db_name;
	ev->table = event.tbl_name;

	for (auto fi = filter.begin(), end = filter.end(); fi != end; ++fi) {
		const auto ri = event.m_row.find(fi->first);
		if (ri != event.m_row.end()) {
			// if it's not a key and event is delete - don't actually delete, just nullify
			ev->row[ fi->second.first ] = !fi->second.second && is_delete ? boost::any() : ri->second;
		}
	}
	stopped = cb(std::move(ev));
}

void DBReader::XidEventCallback(unsigned int server_id, const BinlogEventCallback& cb)
{
	if (stopped) return;
	// last_event_when = ::time(NULL);

	// send binlog position update event
	SerializableBinlogEventPtr ev(new SerializableBinlogEvent);
	ev->binlog_name = state.getMasterLogName();
	ev->binlog_pos = state.getMasterLogPos();
	// ev->seconds_behind_master = GetSecondsBehindMaster();
	// ev->unix_timestamp = long(time(NULL));
	ev->event = "IGNORE";
	stopped = cb(std::move(ev));
}

void DBReader::DumpTablesCallback(
	const std::string& db_name,
	const std::string& tbl_name,
	const std::vector<std::pair<unsigned, slave::PtrField>>& filter,
	const nanomysql::fields_t& fields,
	const BinlogEventCallback& cb
) {
	if (stopped) return;

	SerializableBinlogEventPtr ev(new SerializableBinlogEvent);
	ev->binlog_name = "";
	ev->binlog_pos = 0;
	ev->database = db_name;
	ev->table = tbl_name;
	ev->event = "INSERT";
	// ev->seconds_behind_master = GetSecondsBehindMaster();
	// ev->unix_timestamp = long(time(NULL));

	for (const auto& it : filter) {
		slave::PtrField ptr_field = it.second;
		const auto& field = fields.at(ptr_field->field_name);
		if (field.is_null) {
			ev->row[ it.first ] = boost::any();
		} else {
			ptr_field->unpack_str(field.data);
			ev->row[ it.first ] = ptr_field->field_data;
		}
	}
	stopped = cb(std::move(ev));
}

// unsigned DBReader::GetSecondsBehindMaster() const {
// 	return std::max(::time(NULL) - last_event_when, 0L);
// }

} // replicator
