#pragma once
#include "RNA.h"
#include "TimerQueue/TimerQueue.h"
#include "date/date.h"

USING_BIO_NAMESPACE

namespace Timer
{

class Timer : public RNA
{
	using second_point = std::chrono::time_point<std::chrono::system_clock,
		std::chrono::seconds>;

	struct TimerInfo
	{
		unsigned long long timer_id;
		bool aborted;
	};
public:
	PUBLIC_API Timer(IBiomolecule* owner);
	PUBLIC_API virtual ~Timer();

protected:
	virtual void OnEvent(const DynaArray& name);

private:
	unsigned long long add_timer(const String& id, unsigned long long interval, bool period, unsigned long long at = 0);
	std::chrono::minutes parse_offset(std::istream& in);
	second_point parse(const std::string& str);
	template<typename T, typename Clock, typename Duration>
	std::chrono::seconds since(std::chrono::time_point<Clock, Duration> time);
	template <typename Clock, typename Duration>
	std::chrono::seconds next_year_month(std::chrono::time_point<Clock, Duration> time, unsigned long long next_year, unsigned long long next_month);
	date::year_month_day normalize(date::year_month_day ymd);
	template <typename Clock, typename Duration>
	String get_year(const std::chrono::time_point<Clock, Duration>& time);
	template <typename Clock, typename Duration>
	String get_year_month(const std::chrono::time_point<Clock, Duration>& time);
	template <typename Clock, typename Duration>
	void get_datetime(const std::chrono::time_point<Clock, Duration>& time, String& gmt, String& local_time);
	void RegisterActivity(const String& name, const String& id);
	void UnregisterActivity(const String& name, const String& id);
	std::chrono::time_point<std::chrono::system_clock> GetTodayMidnight();

private:
	TimerQueue timer_queue_;
	Map<String, TimerInfo> timer_id_list_;
	Mutex timer_mutex_;
	
	static Mutex get_time_mutex_;
	static String last_time_;
};

}