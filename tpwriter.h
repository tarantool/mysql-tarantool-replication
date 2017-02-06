#ifndef REPLICATOR_TPWRITER_H
#define REPLICATOR_TPWRITER_H

#include <string>
#include <map>
#include <vector>
#include <tarantool/tarantool.h>
#include <tarantool/tnt_net.h>

#include "serializable.h"

namespace replicator {

class TPWriter
{
	public:
		TPWriter(
			const std::string& host,
			const uint32_t binlog_key_space,
			const unsigned binlog_key,
			const unsigned connect_retry,
			const unsigned sync_retry
		);
		~TPWriter();

		bool Connect();
		void Disconnect();
		void ReadBinlogPos(std::string& binlog_name, unsigned long& binlog_pos);
		void Sync();
		void BinlogEventCallback(SerializableBinlogEventPtr&& ev);
		void RecvAll();

		void AddTable(
			const std::string& db,
			const std::string& table,
			const unsigned space,
			const std::vector<unsigned>& keys,
			const std::string& insert_call = empty_call,
			const std::string& update_call = empty_call,
			const std::string& delete_call = empty_call
		) {
			dbs[db].emplace(
				std::piecewise_construct,
				std::forward_as_tuple(table),
				std::forward_as_tuple(space, keys, insert_call, update_call, delete_call)
			);
		}

		static const std::string empty_call;
		std::map<uint32_t, unsigned> space_last_id;
		std::map<unsigned, std::map<unsigned, SerializableValue>> replace_null;

	private:
		static const unsigned int PING_TIMEOUT = 5000;

		typedef struct ::tnt_stream s_tnt_stream;
		struct __tnt_object : s_tnt_stream {
			__tnt_object() { ::tnt_object((s_tnt_stream*)this); }
			~__tnt_object() { ::tnt_stream_free((s_tnt_stream*)this); }
			inline s_tnt_stream* operator& () { return (s_tnt_stream*)this; }
		};

		struct __tnt_net : s_tnt_stream {
			__tnt_net() { ::tnt_net((s_tnt_stream*)this); }
			~__tnt_net() { ::tnt_stream_free((s_tnt_stream*)this); }
			inline s_tnt_stream* operator& () { return (s_tnt_stream*)this; }
			inline ::tnt_stream_net* net() { return TNT_SNET_CAST(this); }
			inline int fd() { return net()->fd; }
		};

		typedef struct ::tnt_request s_tnt_request;
		struct __tnt_request : s_tnt_request {
			~__tnt_request() { ::tnt_request_free((s_tnt_request*)this); }
			inline s_tnt_request* operator& () { return (s_tnt_request*)this; }
		};

		typedef struct ::tnt_reply s_tnt_reply;
		struct __tnt_reply : s_tnt_reply {
			__tnt_reply() { ::tnt_reply_init((s_tnt_reply*)this); }
			~__tnt_reply() { ::tnt_reply_free((s_tnt_reply*)this); }
			inline s_tnt_reply* operator& () { return (s_tnt_reply*)this; }
		};

		const std::string host;
		const uint32_t binlog_key_space;
		const unsigned binlog_key;
		const unsigned connect_retry;
		const unsigned sync_retry;

		std::string binlog_name;
		unsigned long binlog_pos;
		::time_t next_connect_attempt; /* seconds */
		uint64_t next_sync_attempt; /* milliseconds */
		uint64_t next_ping_attempt; /* milliseconds */
		int sent_cnt;
		__tnt_net sess;

		int64_t Send(struct ::tnt_request *req);
		void Recv(struct ::tnt_reply *re);

		inline void SendRecvSynced(struct ::tnt_request *req, struct ::tnt_reply *re) {
			const uint64_t sync = Send(req);
			Recv(re);
			if (re->sync != sync) {
				throw std::runtime_error("SendRecvSynced() error: bad sync");
			}
		}
		inline void SendRecvSynced(struct ::tnt_request *req) {
			const uint64_t sync = Send(req);
			__tnt_reply re;
			Recv(&re);
			if ((&re)->sync != sync) {
				throw std::runtime_error("SendRecvSynced() error: bad sync");
			}
		}

		inline uint64_t Milliseconds() {
			struct ::timeval tp;
			::gettimeofday(&tp, NULL);
			return (uint64_t)tp.tv_sec * 1000 + tp.tv_usec / 1000;
		}

		struct TableSpace
		{
			TableSpace(
				const uint32_t space_,
				const std::vector<unsigned>& keys_,
				const std::string& insert_call_,
				const std::string& update_call_,
				const std::string& delete_call_
			) :
				space(space_), keys(keys_),
				insert_call(insert_call_), update_call(update_call_), delete_call(delete_call_)
			{}

			const uint32_t space;
			const std::vector<unsigned> keys;
			const std::string insert_call;
			const std::string update_call;
			const std::string delete_call;
		};

		std::map<std::string, std::map<std::string, TableSpace>> dbs;
};

}

#endif // REPLICATOR_TPWRITER_H
