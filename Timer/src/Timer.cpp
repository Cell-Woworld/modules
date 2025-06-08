#include "Timer.h"
#include <ctime>
#include <thread>
#include "../proto/Timer.pb.h"
#include "../proto/Cell.pb.h"
#include <regex>
#include <iomanip>
#include "nlohmann/json.hpp"

#define TAG "Timer"

#define JESUS_LOVE	-62169984000

namespace Timer
{

#ifdef STATIC_API
	extern "C" PUBLIC_API  RNA* Timer_CreateInstance(IBiomolecule* owner)
	{
		return new Timer(owner);
	}
#else
	extern "C" PUBLIC_API RNA* CreateInstance(IBiomolecule* owner)
	{
		return new Timer(owner);
	}
#endif

	Mutex Timer::get_time_mutex_;
	String Timer::last_time_;

	Timer::Timer(IBiomolecule* owner)
		:RNA(owner, "Timer", this)
	{
		init();
	}

	Timer::~Timer()
	{
		MutexLocker _timer_guard(this->timer_mutex_);
		for (const auto& elem : timer_id_list_)
		{
			UnregisterActivity("Timer.Set", elem.first);
		}
		timer_id_list_.clear();
		timer_queue_.cancelAll();
		//while (!timer_id_list_.empty())
		//{
		//	timer_queue_.cancel(timer_id_list_.begin()->second);
		//	timer_id_list_.erase(timer_id_list_.begin());
		//}
	}

	void Timer::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "Timer.Set"_hash:
		{
			MutexLocker _timer_guard(this->timer_mutex_);
			unsigned long long _interval = ReadValue<unsigned long long>(_name + ".interval");
			String _id = ReadValue<String>(_name + ".id");
			bool _period = ReadValue<bool>(_name + ".period");
			String _specific = ReadValue<String>(_name + ".specific");
			long long _offset = ReadValue<long long>(_name + ".offset");
			if (_specific != "")
			{
				long long _epoch = parse(_specific).time_since_epoch().count();
				if (_epoch > time(NULL))
				{
					auto _timer_id = add_timer(_id, 0, false, 1000 * (_epoch - time(NULL)) + _offset);
					LOG_I(TAG, "****** %s: %s, one shot timeout at %s ******", _name.c_str(), _id.c_str(), _specific.c_str());
					//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
					timer_id_list_[_id] = TimerInfo({ _timer_id, false });

					RegisterActivity(_name, _id);
				}
				else 
				{
					if (_interval % ((unsigned long long)365 * 24 * 60 * 60 * 1000) == 0)
					{	// 0xxx-xx-xxTxx:xx:xx
						using namespace std::chrono;
						_specific = get_year(system_clock::now()) + _specific.substr(4);
						time_point _specific_time_point = parse(_specific);
						_epoch = _specific_time_point.time_since_epoch().count();
						long long _now = floor<seconds>(system_clock::now().time_since_epoch()).count();
						unsigned long long _interval_from_now = (_epoch > _now ? _epoch - _now : this->next_year_month(_specific_time_point, _interval / ((unsigned long long)365 * 24 * 60 * 60 * 1000), 0).count() - _now);
						auto _timer_id = add_timer(_id, _interval, _period, 1000 * _interval_from_now + _offset);
						LOG_I(TAG, "****** %s: %s, %lld yearly timeout at %s ******", _name.c_str(), _id.c_str(), _interval / ((unsigned long long)365 * 24 * 60 * 60 * 1000), _specific.c_str());
						//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
						timer_id_list_[_id] = TimerInfo({ _timer_id, false });

						RegisterActivity(_name, _id);
					}
					else if (_interval % ((unsigned long long)30 * 24 * 60 * 60 * 1000) == 0)
					{	// 0000-xx-xxTxx:xx:xx
						using namespace std::chrono;
						_specific = get_year_month(system_clock::now()) + _specific.substr(7);
						time_point _specific_time_point = parse(_specific);
						_epoch = _specific_time_point.time_since_epoch().count();
						long long _now = floor<seconds>(system_clock::now().time_since_epoch()).count();
						unsigned long long _interval_from_now = (_epoch > _now ? _epoch - _now : this->next_year_month(_specific_time_point, 0, _interval / ((unsigned long long)30 * 24 * 60 * 60 * 1000)).count() - _now);
						auto _timer_id = add_timer(_id, _interval, _period, 1000 * _interval_from_now + _offset);
						LOG_I(TAG, "****** %s: %s, %llu monthly timeout %llu seconds later******", _name.c_str(), _id.c_str(), _interval / ((unsigned long long)30 * 24 * 60 * 60 * 1000), _interval_from_now + _offset/1000);
						//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
						timer_id_list_[_id] = TimerInfo({ _timer_id, false });

						RegisterActivity(_name, _id);
					}
					else if (_interval % ((unsigned long long)7 * 24 * 60 * 60 * 1000) == 0)
					{	// 0000-00-xxTxx:xx:xx
						_epoch = (_epoch - JESUS_LOVE) % ((unsigned long long)7 * 24 * 60 * 60);
						long long _now = since<date::weeks>(std::chrono::system_clock::now()).count();
						auto _timer_id = add_timer(_id, _interval, _period, 1000 * (_epoch + _offset / 1000 > _now ? _epoch + _offset / 1000 - _now : _epoch + _offset / 1000 + ((unsigned long long)7 * 24 * 60 * 60) - _now));
						LOG_I(TAG, "****** %s: %s, %lld weekly timeout at %s ******", _name.c_str(), _id.c_str(), _interval / ((unsigned long long)7 * 24 * 60 * 60 * 1000), _specific.c_str());
						//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
						timer_id_list_[_id] = TimerInfo({ _timer_id, false });

						RegisterActivity(_name, _id);
					}
					else if (_interval % ((unsigned long long)24 * 60 * 60 * 1000) == 0)
					{	// 0000-00-00Txx:xx:xx
						_epoch = (_epoch < JESUS_LOVE ? _epoch + 24 * 60 * 60 : _epoch);
						_epoch = (_epoch - JESUS_LOVE) % ((unsigned long long)24 * 60 * 60);
						long long _now = since<date::days>(std::chrono::system_clock::now()).count();
						auto _timer_id = add_timer(_id, _interval, _period, 1000 * (_epoch + _offset / 1000 > _now ? _epoch + _offset / 1000 - _now : _epoch + _offset / 1000 + ((unsigned long long)24 * 60 * 60) - _now));
						LOG_I(TAG, "****** %s: %s, %lld daily timeout at %s ******", _name.c_str(), _id.c_str(), _interval / ((unsigned long long)24 * 60 * 60 * 1000), _specific.c_str());
						//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
						timer_id_list_[_id] = TimerInfo({ _timer_id, false });

						RegisterActivity(_name, _id);
					}
					else if (_interval % ((unsigned long long)60 * 60 * 1000) == 0)
					{	// 0000-00-00T00:xx:xx
						if (_epoch < JESUS_LOVE)
							return;
						_epoch = (_epoch - JESUS_LOVE) % ((unsigned long long)60 * 60);
						long long _now = since<std::chrono::hours>(std::chrono::system_clock::now()).count();
						auto _timer_id = add_timer(_id, _interval, _period, 1000 * (_epoch + _offset / 1000 > _now ? _epoch + _offset / 1000 - _now : _epoch + _offset / 1000 + ((unsigned long long)60 * 60) - _now));
						LOG_I(TAG, "****** %s: %s, %lld hourly timeout at %s ******", _name.c_str(), _id.c_str(), _interval / ((unsigned long long)60 * 60 * 1000), _specific.c_str());
						//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
						timer_id_list_[_id] = TimerInfo({ _timer_id, false });

						RegisterActivity(_name, _id);
					}
					else
					{
						LOG_E(TAG, "Invalid interval %lld", _interval);
					}
				}
			}
			else
			{
				auto _timer_id = add_timer(_id, _interval, _period, 1000 * (_offset < 0 ? (_interval + _offset / 1000 > 0 ? _interval + _offset / 1000 : 0) : _offset / 1000));
				LOG_I(TAG, "****** %s: %s,%lld ******", _name.c_str(), _id.c_str(), _timer_id);
				//timer_id_list_.insert(std::make_pair(_id, TimerInfo({ _timer_id, false })));
				timer_id_list_[_id] = TimerInfo({ _timer_id, false });

				RegisterActivity(_name, _id);
			}
			break;
		}
		case "Timer.Cancel"_hash:
		{
			MutexLocker _timer_guard(this->timer_mutex_);
			String _id = ReadValue<String>(_name + ".id");
			if (timer_id_list_.count(_id) > 0) {
				LOG_D(TAG, "****** %s: %s %s ******", _name.c_str(), _id.c_str(), (timer_id_list_.count(_id) > 0?" found":" not found"));
				timer_queue_.cancel(timer_id_list_[_id].timer_id);
				timer_id_list_[_id].aborted = true;
				timer_id_list_[_id].timer_id = -1;
				UnregisterActivity("Timer.Set", _id);
			}
			break;
		}
		case "Timer.CancelAll"_hash:
		{
			MutexLocker _timer_guard(this->timer_mutex_);
			for (const auto& elem : timer_id_list_)
			{
				UnregisterActivity("Timer.Set", elem.first);
			}
			timer_id_list_.clear();
			timer_queue_.cancelAll();
			break;
		}
		case "Timer.Now"_hash:
		{
			using namespace std::chrono;
			const std::regex NONISO_TIME_EXPR1("^\\d\\d\\d\\d(0[1-9]|1[0-2])(0[1-9]|[12][0-9]|3[01])(0[0-9]|1[0-9]|2[0-3])([0-9]|[0-5][0-9])([0-9]|[0-5][0-9])$");
			const std::regex NONISO_TIME_EXPR2("^\\d\\d\\d\\d[-/](0[1-9]|1[0-2])[-/](0[1-9]|[12][0-9]|3[01]) (0[0-9]|1[0-9]|2[0-3]):([0-9]|[0-5][0-9]):([0-9]|[0-5][0-9])($| [+-](0[1-9]|1[0-2])\\d\\d$)");
			const std::regex DURATION_EXPR("^-?\\d\\d\\d\\d[-/](0[0-9]|1[0-2])[-/](0[0-9]|[12][0-9]|3[01])[ T](0[0-9]|1[0-9]|2[0-3]):([0-9]|[0-5][0-9]):([0-9]|[0-5][0-9])($| [+-](0[1-9]|1[0-2])\\d\\d$)");
			String _offset = ReadValue<String>(_name + ".offset");
			String _specific = ReadValue<String>(_name + ".specific");
			String _target_model_name = ReadValue<String>(_name + ".target_model_name");
			bool _absolute = ReadValue<bool>(_name + ".absolute");
			unsigned long long _epoch = ReadValue<unsigned long long>(_name + ".epoch");
			time_point<system_clock> _now;
			if (_specific != "")
			{
				if (std::regex_match(_specific, NONISO_TIME_EXPR1))
				{
					StringBuilder _sb;
					const int DATETIME_FORMAT[] = { 4,2,2,2,2,2 };
					for (int i = 0, start = 0; i < sizeof(DATETIME_FORMAT) / sizeof(int); start += DATETIME_FORMAT[i], i++)
					{
						_sb << _specific.substr(start, DATETIME_FORMAT[i]);
						switch (i) 
						{
						case 0:
						case 1:
							_sb << '-';
							break;
						case 2:
							_sb << 'T';
							break;
						case 3:
						case 4:
							_sb << ':';
							break;
						default:
							break;
						}
					}
					_specific = _sb.str();
				}
				else if (std::regex_match(_specific, NONISO_TIME_EXPR2))
				{
					size_t _pos = _specific.find_first_of(' ');
					if (_pos != String::npos)
					{
						_specific[_pos] = 'T';
					}
					if (_specific.size() == 19)
					{
						_specific.push_back('Z');		// apply standard format to avoid "exception:ios_base::failbit set: iostream stream error"
					}
				}

				try
				{
					_now = parse(_specific);
				}
				catch (const std::exception& e)
				{
					LOG_E(TAG, "Invalid specific time %s! Replace it with system_clock::now().\n    Exception: %s", ReadValue<String>(_name + ".specific").c_str(), e.what());
					if (_absolute)
						_now = GetTodayMidnight();
					else
						_now = system_clock::now();
					
				}
			}
			else if (_epoch != 0) {
				time_point<system_clock, milliseconds> tp{ std::chrono::milliseconds{_epoch} };
				_now = tp;
			}
			else
			{
				if (_absolute)
					_now = GetTodayMidnight();
				else
					_now = system_clock::now();
			}
			if (_offset != "")
			{
				if (!std::regex_match(_offset, DURATION_EXPR))
				{
					try
					{
						_now += std::chrono::seconds(stoll(_offset));
					}
					catch (const std::exception& e)
					{
						LOG_E(TAG, "stoll(%s) exception:%s", _offset.c_str(), e.what());
					}
				}
				else
				{
					using namespace date;
					try
					{
						time_point<system_clock> _offset_tp = parse(_offset);
						
						auto daypoint = floor<days>(_now);
						auto ymd = year_month_day(daypoint);

						daypoint = floor<days>(_offset_tp);
						auto ymd2 = year_month_day(daypoint);

						auto y1 = (int)ymd.year() + (int)ymd2.year();
						auto m1 = (ymd.month() - January).count() + (ymd2.month() - January).count() + 1 + 1;
						auto d1 = (ymd.day() - 0_d).count() + (ymd2.day() - 0_d).count();
						ymd = year((int)y1) / (int)m1 / (int)d1;		// Yields a year_month_day type

						if (!ymd.ok())
						{
							//throw std::runtime_error("Invalid date");
							ymd = normalize(ymd);
						}
						_now = sys_days(ymd);
					}
					catch (const std::exception& e)
					{
						LOG_E(TAG, "Invalid specific time %s! Replace it with system_clock::now().\n    Exception: %s", ReadValue<String>(_name + ".specific").c_str(), e.what());
					}
				}
			}
			String _gmt, _local_time;
			get_datetime(_now, _gmt, _local_time);
			if (_specific == "" && !_absolute && _epoch==0)
			{
				MutexLocker _timer_guard(this->timer_mutex_);
				while (_gmt == last_time_) {
					std::this_thread::sleep_for(std::chrono::microseconds(1));
					time_point _now = system_clock::now();
					get_datetime(_now, _gmt, _local_time);
				}
				last_time_ = _gmt;
			}
			const int DATETIME_SECTION_COUNT[] = { 4, 2, 2, 2, 2, 2, 6 };
			if (_target_model_name != "")
			{
				using json = nlohmann::json;
				json _output;
				_output["id"] = ReadValue<String>(_name + ".id");
				_output["utc"] = json::array();
				_output["local_time"] = json::array();
				int _pos = 0;
				for (int i = 0; i < sizeof(DATETIME_SECTION_COUNT) / sizeof(int); i++)
				{
					_output["utc"].push_back(_gmt.substr(_pos, DATETIME_SECTION_COUNT[i]));
					_output["local_time"].push_back(_local_time.substr(_pos, DATETIME_SECTION_COUNT[i]));
					_pos += DATETIME_SECTION_COUNT[i];
				}
				_output["epoch"] = floor<microseconds>(_now.time_since_epoch()).count();
				WriteValue(_target_model_name, _output.dump());
			}
			if (ReadValue<String>("Bio.Cell.Current.Event") == _name)
			{
				Now::Result _result;
				_result.set_id(ReadValue<String>(_name + ".id"));
				google::protobuf::RepeatedPtrField<String>& _utc = *_result.mutable_utc();
				google::protobuf::RepeatedPtrField<String>& _local = *_result.mutable_localtime();
				int _pos = 0;
				for (int i = 0; i < sizeof(DATETIME_SECTION_COUNT) / sizeof(int); i++)
				{
					_utc.Add(_gmt.substr(_pos, DATETIME_SECTION_COUNT[i]));
					_local.Add(_local_time.substr(_pos, DATETIME_SECTION_COUNT[i]));
					_pos += DATETIME_SECTION_COUNT[i];
				}
				_result.set_epoch(floor<microseconds>(_now.time_since_epoch()).count());
				SendEvent(Now::Result::descriptor()->full_name(), _result.SerializeAsString());
			}
			break;
		}
		default:
			break;
		}
	}

	unsigned long long Timer::add_timer(const String& id, unsigned long long interval, bool period, unsigned long long at)
	{
		unsigned long long _start_at = (at != 0) ? at : interval;
		return timer_queue_.add(_start_at, [this, id, interval, period](bool aborted) mutable {
			if (aborted == true)
			{
				MutexLocker _timer_guard(this->timer_mutex_);
				if (!timer_id_list_.empty() && timer_id_list_.find(id) != timer_id_list_.end())
					timer_id_list_.erase(id);
			}
			else
			{
				MutexLocker _timer_guard(this->timer_mutex_);
				if (timer_id_list_.find(id) == timer_id_list_.end())
					return;

				if (!timer_id_list_[id].aborted)
					this->SendEvent("Timer.Timeout." + id);
				if (period && !timer_id_list_[id].aborted)
				{
					if (interval <= ((unsigned long long)7 * 24 * 60 * 60 * 1000))
						add_timer(id, interval, period);
					else if (interval % ((unsigned long long)30 * 24 * 60 * 60 * 1000) == 0)
					{
						add_timer(id, this->next_year_month(std::chrono::system_clock::now(), 0, interval / ((unsigned long long)30 * 24 * 60 * 60 * 1000)).count() * 1000, period);
					}
					else if (interval % ((unsigned long long)365 * 24 * 60 * 60 * 1000) == 0)
					{
						add_timer(id, this->next_year_month(std::chrono::system_clock::now(), interval / ((unsigned long long)365 * 24 * 60 * 60 * 1000), 0).count() * 1000, period);
					}
				}
				else
				{
					if (!timer_id_list_.empty() && timer_id_list_.find(id) != timer_id_list_.end())
					{
						timer_id_list_.erase(id);
						//UnregisterActivity("Timer.Set", id);
					}
				}
			}
			});
	}

	std::chrono::minutes Timer::parse_offset(std::istream& in)
	{
		using namespace std::chrono;
		char c;
		in >> c;
		minutes result = 10 * hours{ c - '0' };
		in >> c;
		result += hours{ c - '0' };
		try
		{
			in >> c;
			if (c == ':')
				in >> c;
			result += 10 * minutes{ c - '0' };
			in >> c;
			result += minutes{ c - '0' };
		}
		catch (const std::exception & e)
		{
			std::cout << "exception from parse_offset(): " << e.what() << std::endl;
		}
		return result;
	};

	using second_point = std::chrono::time_point<std::chrono::system_clock,
		std::chrono::seconds>;

	second_point Timer::parse(const std::string& str)
	{
		std::istringstream in(str);
		in.exceptions(std::ios::failbit | std::ios::badbit);
		int yi, mi, di;
		char dash;
		// check dash if you're picky
		in >> yi >> dash >> mi >> dash >> di;
		using namespace date;
		auto ymd = year{ yi } / mi / di;
		// check ymd.ok() if you're picky
		char T;
		in >> T;
		// check T if you're picky
		int hi, si;
		char colon;
		in >> hi >> colon >> mi >> colon >> si;
		// check colon if you're picky
		using namespace std::chrono;
		auto h = hours{ hi };
		auto m = minutes{ mi };
		auto s = seconds{ si };
		second_point result = sys_days{ ymd } +h + m + s;
		try
		{
			char f;
			in >> f;
			if (f == '+')
				result -= parse_offset(in);
			else if (f == '-')
				result += parse_offset(in);
			else
				;// check f == 'Z' if you're picky
		}
		catch (const std::exception & e)
		{
			LOG_W(TAG, "exception from parse(%s). exception:%s", str.c_str(), e.what());
		}
		return result;
	}

	template<typename T, typename Clock, typename Duration>
	std::chrono::seconds Timer::since(std::chrono::time_point<Clock, Duration> time)
	{
		using namespace std::chrono;
		time_point tp = floor<T>(time);
		return floor<seconds>(std::chrono::system_clock::now() - tp);
	}

	template <typename Clock, typename Duration>
	std::chrono::seconds Timer::next_year_month(std::chrono::time_point<Clock, Duration> time, unsigned long long next_year, unsigned long long next_month)
	{
		using namespace std::chrono;
		using namespace date;
		//auto time = std::chrono::system_clock::now();
		//auto daypoint = floor<days>(time);
		//auto ymd = year_month_day(daypoint);
		//auto tod = make_time(time - daypoint); // Yields time_of_day type

		//auto y = (int)ymd.year() + next_year;
		//auto m = (ymd.month() - January).count() + next_month + 1;
		//auto d = (ymd.day() - 0_d).count();
		std::time_t _time = std::chrono::system_clock::to_time_t(time);
		tm* _tm = std::localtime(&_time);
		auto y = _tm->tm_year + next_year + 1900;
		auto m = _tm->tm_mon + next_month + 1;
		auto d = _tm->tm_mday;

		auto ymd = year((int)y) / (int)m / (int)d;		// Yields a year_month_day type

		if (!ymd.ok())
		{ 
			//throw std::runtime_error("Invalid date");
			ymd = normalize(ymd);
		}
		_tm->tm_year = (int)ymd.year() - 1900;
		_tm->tm_mon = (ymd.month() - January).count();
		_tm->tm_mday = (ymd.day() - 0_d).count();
		const auto tp = std::chrono::system_clock::from_time_t(mktime(_tm));
		//time_point<Clock, Duration> tp = sys_days(ymd) + tod.to_duration();
			//hours(t.tm_hour) + minutes(t.tm_min) + seconds(t.tm_sec);
		return floor<seconds>(tp.time_since_epoch());
	}

	date::year_month_day Timer::normalize(date::year_month_day ymd)
	{
		using namespace date;
		ymd += months{ 0 };
		ymd = sys_days{ ymd };
		return ymd;
	}

	template <typename Clock, typename Duration>
	String Timer::get_year(const std::chrono::time_point<Clock, Duration>& time)
	{
		using namespace std::chrono;
		using namespace date;
		auto daypoint = floor<days>(time);
		auto ymd = year_month_day(daypoint);
		return std::to_string((int)ymd.year());
	}

	template <typename Clock, typename Duration>
	String Timer::get_year_month(const std::chrono::time_point<Clock, Duration>& time)
	{
		using namespace std::chrono;
		using namespace date;
		auto daypoint = floor<days>(time);
		auto ymd = year_month_day(daypoint);
		std::stringstream ss;
		ss << ymd.year() << '-' << std::setfill('0') << std::setw(2) << (ymd.month() - January).count() + 1;
		return ss.str();
	}

	template <typename Clock, typename Duration>
	void Timer::get_datetime(const std::chrono::time_point<Clock, Duration>& time, String& gmt, String& local_time)
	{
		using namespace std::chrono;
		std::time_t _time = std::chrono::system_clock::to_time_t(time);

		gmt.assign(20, '0');
		std::strftime(&gmt[0], gmt.size(), "%Y%m%d%H%M%S", std::gmtime(&_time));
		gmt[4 + 2 + 2 + 2 + 2 + 2] = '0';
		auto _micro_seconds = (duration_cast<std::chrono::microseconds>(time.time_since_epoch())
			- duration_cast<std::chrono::seconds>(time.time_since_epoch())).count();
		String _micro_seconds_str = std::to_string((unsigned int)_micro_seconds);
		_micro_seconds_str.copy((char*)&gmt.data()[20 - _micro_seconds_str.size()], _micro_seconds_str.size());

		local_time.assign(20, '0');
		std::strftime(&local_time[0], local_time.size(), "%Y%m%d%H%M%S", std::localtime(&_time));
		local_time[4 + 2 + 2 + 2 + 2 + 2] = '0';
		_micro_seconds_str.copy((char*)&local_time.data()[20 - _micro_seconds_str.size()], _micro_seconds_str.size());
	}

	void Timer::RegisterActivity(const String& name, const String& id)
	{
		Bio::Chromosome::RegisterActivity _register_activity;
		_register_activity.set_id(id);
		_register_activity.set_name(name);
		_register_activity.set_payload(ReadValue<String>(String("encode.") + name));
		SendEvent("Bio.Chromosome.RegisterActivity", _register_activity.SerializeAsString());
	}

	void Timer::UnregisterActivity(const String& name, const String& id)
	{
		Bio::Chromosome::UnregisterActivity _unregister_activity;
		_unregister_activity.set_id(id);
		_unregister_activity.set_name(name);
		SendEvent("Bio.Chromosome.UnregisterActivity", _unregister_activity.SerializeAsString());
	}

	std::chrono::time_point<std::chrono::system_clock> Timer::GetTodayMidnight()
	{
		using namespace std::chrono;
		std::time_t _time = system_clock::to_time_t(system_clock::now());
		tm* _local_tm = std::localtime(&_time);
		_local_tm->tm_hour = 0;
		_local_tm->tm_min = 0;
		_local_tm->tm_sec = 0;
		time_t _local_time_t = std::mktime(_local_tm);
		return time_point<system_clock>(std::chrono::seconds{ _local_time_t });
		//_now = system_clock::from_time_t(std::mktime(_local_tm));
		//_now = time_point<system_clock>(std::chrono::seconds{ floor<date::days>(_local_now).time_since_epoch().count() });
		//_now = floor<date::days>(system_clock::now());
	}
}