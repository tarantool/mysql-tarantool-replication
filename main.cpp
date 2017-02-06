#include <time.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <thread>
#include <signal.h>

#include <yaml-cpp/yaml.h>

#include "serializable.h"
#include "dbreader.h"
#include "tpwriter.h"
#include "serializable.h"
#include "queue.h"
#include "logger.h"

// =========
using std::placeholders::_1;

namespace replicator {

static const char *default_pid_filename = "/var/run/replicatord.pid";
static const char *default_log_filename = "/var/log/replicatord.log";
static const char *default_config_filename = "/usr/local/etc/replicatord.cfg";

static volatile bool is_term = false;
static volatile bool reset = false;

static std::string binlog_name;
static unsigned long binlog_pos;

static TPWriter *tpwriter = NULL;
static DBReader *dbreader = NULL;

static void sigint_handler(int sig);

static Queue<SerializableBinlogEventPtr> queue(50);

// ===============

static void tpwriter_worker()
{
	while (!is_term)
	{
		while (!tpwriter->Connect());

		// send initial binlog position to the db thread
		try {
			tpwriter->ReadBinlogPos(binlog_name, binlog_pos);
			reset = true;
			const std::chrono::milliseconds timeout(1000);

			while (!is_term) {
				for (unsigned cnt = queue.wait(timeout); cnt > 0; --cnt) {
					tpwriter->BinlogEventCallback(queue.pop());
				}
				tpwriter->Sync();
				tpwriter->RecvAll();
			}
		}
		catch (std::range_error& ex) {
			is_term = true;
			std::cout << ex.what() << std::endl;
			// loop exit
		}
		catch (std::exception& ex) {
			std::cout << ex.what() << std::endl;
			tpwriter->Disconnect();
			// reconnect
		}
	}

	tpwriter->Disconnect();
}

// ====================

static bool dbread_callback(SerializableBinlogEventPtr&& ev)
{
	if (is_term || reset) return true;
	queue.push(std::forward<SerializableBinlogEventPtr>(ev));
	return false;
}

static void mysql_worker()
{
	while (!is_term) {
		// read initial binlog pos from Tarantool
		while (!reset) ::sleep(1);
		reset = false;

		try {
			if (!is_term && !reset && binlog_name == "" && binlog_pos == 0) {
				std::cout << "Tarantool reported null binlog position. Dumping tables..." << std::endl;
				dbreader->DumpTables(binlog_name, binlog_pos, dbread_callback);
			}
			if (!is_term && !reset) {
				std::cout << "Reading binlogs (" << binlog_name << ", " << binlog_pos << ")..." << std::endl;
				dbreader->ReadBinlog(binlog_name, binlog_pos, dbread_callback);
			}
		}
		catch (std::exception& ex) {
			std::cerr << "Error in reading binlogs: " << ex.what() << std::endl;
			sigint_handler(0);
		}
	}
}

static void init(YAML::Node& cfg)
{
	try {
		// read Mysql settings
		{
			const YAML::Node& mysql = cfg["mysql"];

			nanomysql::mysql_conn_opts opts;
			opts.mysql_host = mysql["host"].as<std::string>();
			opts.mysql_port = mysql["port"].as<unsigned>();
			opts.mysql_user = mysql["user"].as<std::string>();
			opts.mysql_pass = mysql["password"].as<std::string>();

			dbreader = new DBReader(opts, mysql["connect_retry"].as<unsigned>());
		}
		// read Tarantool config
		{
			const YAML::Node& tarantool = cfg["tarantool"];

			tpwriter = new TPWriter(
				tarantool["host"].as<std::string>(),
				tarantool["binlog_pos_space"].as<unsigned>(),
				tarantool["binlog_pos_key"].as<unsigned>(),
				tarantool["connect_retry"].as<unsigned>(),
				tarantool["sync_retry"].as<unsigned>()
			);
		}
		// read Mysql to Tarantool mappings (each table maps to a single Tarantool space)
		{
			std::map<unsigned, bool> has_primary;
			const YAML::Node& mappings = cfg["mappings"];

			for (int i = 0; i < mappings.size(); i++) {
				const YAML::Node& mapping = mappings[i];

				const std::string database = mapping["database"].as<std::string>();
				const std::string table = mapping["table"].as<std::string>();

				std::string insert_call = mapping["insert_call"] ? mapping["insert_call"].as<std::string>() : TPWriter::empty_call;
				std::string update_call = mapping["update_call"] ? mapping["update_call"].as<std::string>() : TPWriter::empty_call;
				std::string delete_call = mapping["delete_call"] ? mapping["delete_call"].as<std::string>() : TPWriter::empty_call;

				const unsigned space = mapping["space"].as<unsigned>();
				std::map<std::string, std::pair<unsigned, bool>> columns;
				std::vector<unsigned> keys;
				unsigned index_max = tpwriter->space_last_id[space];

				bool is_primary;
				if (has_primary.find(space) == has_primary.end()) {
					is_primary = has_primary[space] = true;;
				} else {
					is_primary = false;
				}

				// read key tarantool fields we'll use for delete requests
				{
					const YAML::Node& keys_ = mapping["key_fields"];
					for (int i = 0; i < keys_.size(); i++) {
						unsigned key = keys_[i].as<unsigned>();
						index_max = std::max(index_max, key);
						keys.push_back(key);
					}
				}
				// read columns tuple
				{
					const YAML::Node& columns_ = mapping["columns"];
					for (int i = 0; i < columns_.size(); i++) {
						const bool is_key = i < keys.size();
						const unsigned index = is_key ? keys[i] : ++index_max;
						columns.emplace(
							std::piecewise_construct,
							std::forward_as_tuple(columns_[i].as<std::string>()),
							std::forward_as_tuple(index, is_key)
						);
					}
				}

				dbreader->AddTable(database, table, columns, is_primary);
				std::sort(keys.begin(), keys.end());
				tpwriter->AddTable(database, table, space, keys, insert_call, update_call, delete_call);
				tpwriter->space_last_id[space] = index_max;
			}
		}
		// read space settings
		{
			const YAML::Node& spaces = cfg["spaces"];

			for (auto its = spaces.begin(); its != spaces.end(); ++its) {
				unsigned space = its->first.as<unsigned>();
				std::map<unsigned, SerializableValue>& rn_ = tpwriter->replace_null[ space ];
				const YAML::Node& replace_null = its->second["replace_null"];

				for (auto itrn = replace_null.begin(); itrn != replace_null.end(); ++itrn) {
					const YAML::Node& field = itrn->second;
					auto itf = field.begin();
					if (itf == field.end()) continue;

					const unsigned index = itrn->first.as<unsigned>();
					std::string type = itf->first.as<std::string>();
					const YAML::Node& value = itf->second;

					if (type == "str" || type == "string") {
						rn_[ index ] = value.as<std::string>();
					} else if (type == "unsigned") {
						rn_[ index ] = value.as<unsigned long long>();
					} else if (type == "int" || type == "integer") {
						rn_[ index ] = value.as<long long>();
					} else {
						std::cerr << "Config error: unknown type for non-null value for column " << index << std::endl;
						exit(EXIT_FAILURE);
					}
				}
			}
		}
	}
	catch(YAML::Exception& ex)
	{
		std::cerr << "Config error: " << ex.what() << std::endl;
		exit(EXIT_FAILURE);
	}
}

static void shutdown()
{
	if (dbreader) {
		// sighandler protection
		auto dbreader_ = dbreader;
		dbreader = NULL;
		delete dbreader_;
	}
	if (tpwriter) {
		auto tpwriter_ = tpwriter;
		tpwriter = NULL;
		delete tpwriter_;
	}
}

static void sigint_handler(int sig)
{
	std::cerr << "Terminating" << std::endl;
	is_term = true;

	if (dbreader) {
		dbreader->Stop();
	}
}

}

static replicator::Logger *ol, *el;
static std::streambuf *ol_sink, *el_sink;
static std::ofstream *flog;

static std::string log_filename(replicator::default_log_filename);
static std::string pid_filename(replicator::default_pid_filename);

static void writepidtofile()
{
	// write pid to file
	std::ofstream fpid(pid_filename);
	fpid << getpid();
	fpid.flush();
	fpid.close();
}

static void removepidfile()
{
	unlink(pid_filename.c_str());
}

static void openlogfile()
{
	flog = new std::ofstream(log_filename, std::ofstream::app);

	// redirect cout and cerr streams, appending timestamps and log levels
	ol = new replicator::Logger(std::cout, 'I');
	el = new replicator::Logger(std::cerr, 'E');

	ol_sink = ol->rdsink();
	el_sink = el->rdsink();

	// redirect loggers to file
	ol->rdsink(flog->rdbuf());
	el->rdsink(flog->rdbuf());
}

static void closelogfile()
{
	if (flog == NULL) {
		return;
	}

	flog->flush();
	flog->close();

	delete flog;
	flog = NULL;

	// restore streams
	ol->rdsink(ol_sink);
	el->rdsink(el_sink);

	delete ol;
	delete el;

	ol = NULL;
	el = NULL;
}

static void sighup_handler(int sig)
{
	closelogfile();
	openlogfile();
	std::cout << "Caught SIGHUP, continuing..." << std::endl;
}

int main(int argc, char** argv)
{
	bool print_usage = false;
	std::string config_name(replicator::default_config_filename);

	int c;
	while (-1 != (c = ::getopt(argc, argv, "c:l:i:zp")))
	{
		switch (c)
		{
			case 'c': config_name = optarg; break;
			case 'p': print_usage = true; break;
			case 'l': log_filename = optarg; break;
			case 'i': pid_filename = optarg; break;
			default: print_usage = true; break;
		}
	}

	if (print_usage) {
		std::cout
			<< "Usage: " << argv[0] << " [-c <config_name>]" << " [-l <log_name>]"<< " [-i <pid_name>]" << " [-p]" << std::endl
			<< " -c configuration file (" << config_name << ")" << std::endl
			<< " -p print usage" << std::endl
			<< " -l log filename (" << log_filename << ")" << std::endl
			<< " -i pid filename (" << pid_filename << ")" << std::endl
			;
		return 1;
	}

	writepidtofile();
	atexit(removepidfile);

	openlogfile();
	atexit(closelogfile);

	YAML::Node cfg;

	// Read the file. If there is an error, report it and exit.
	try {
		cfg = YAML::LoadFile(config_name.c_str());
	}
	catch(YAML::Exception& ex)
	{
		std::cerr << "Config error: " << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	signal(SIGINT, replicator::sigint_handler);
	signal(SIGTERM, replicator::sigint_handler);
	signal(SIGHUP, sighup_handler);

	replicator::init(cfg);

	std::thread t1(replicator::mysql_worker);
	std::thread t2(replicator::tpwriter_worker);

	t2.join();
	t1.join();

	replicator::shutdown();

	return 0;
}
