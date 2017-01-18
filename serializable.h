#ifndef REPLICATOR_SERIALIZABLE_H
#define REPLICATOR_SERIALIZABLE_H

#include <string>
#include <map>
#include <sstream>
#include <memory>
#include <boost/any.hpp>

namespace replicator {

class SerializableValue {
	private:
		boost::any value;

	public:
		template<typename T>
		inline SerializableValue& operator= (T&& value_) {
			value = boost::any(std::forward<T>(value_));
			return *this;
		}

		inline SerializableValue& operator= (boost::any&& value_) {
			value = std::forward<boost::any>(value_);
			return *this;
		}

		template<typename T> inline bool is() const {
			return value.type() == typeid(T);
		}

		template<typename T> inline T as() const {
			return boost::any_cast<T>(value);
		}

		std::string to_string() const {
			std::ostringstream s;

			if (is<std::string()>()) {
				s << as<std::string>();
			} else if (is<char>()) {
				s << as<char>();
			} else if (is<unsigned char>()) {
				s << as<unsigned char>();
			} else if (is<short>()) {
				s << as<short>();
			} else if (is<unsigned short>()) {
				s << as<unsigned short>();
			} else if (is<int>()) {
				s << as<int>();
			} else if (is<unsigned int>()) {
				s << as<unsigned int>();
			} else if (is<long>()) {
				s << as<long>();
			} else if (is<unsigned long>()) {
				s << as<unsigned long>();
			} else if (is<long long>()) {
				s << as<long long>();
			} else if (is<unsigned long long>()) {
				s << as<unsigned long long>();
			} else if (is<float>()) {
				s << as<float>();
			} else if (is<double>()) {
				s << as<double>();
			}
			return s.str();
		}
};


struct SerializableBinlogEvent
{
	std::string binlog_name;
	unsigned long binlog_pos;
	// unsigned long seconds_behind_master;
	// unsigned long unix_timestamp;
	std::string database;
	std::string table;
	std::string event;
	std::map<unsigned, SerializableValue> row;
};

typedef std::unique_ptr<SerializableBinlogEvent> SerializableBinlogEventPtr;

} // replicator

#endif // REPLICATOR_SERIALIZABLE_H
