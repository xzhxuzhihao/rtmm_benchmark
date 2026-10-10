#ifndef _DISB_ANALYZER_H_
#define _DISB_ANALYZER_H_

#include <list>
#include <chrono>
#include <memory>
#include <jsoncpp/json/json.h>
#include <string>
#include <vector>

namespace DISB {

class Analyzer;
class Client;

struct TimePoints
{
    std::chrono::system_clock::time_point prepareInputBegin;
    std::chrono::system_clock::time_point prepareInputEnd;

    std::chrono::system_clock::time_point preprocessBegin;
    std::chrono::system_clock::time_point preprocessEnd;

    std::chrono::system_clock::time_point copyInputBegin;
    std::chrono::system_clock::time_point copyInputEnd;

    std::chrono::system_clock::time_point inferBegin;
    std::chrono::system_clock::time_point inferEnd;

    std::chrono::system_clock::time_point copyOutputBegin;
    std::chrono::system_clock::time_point copyOutputEnd;

    std::chrono::system_clock::time_point postprocessBegin;
    std::chrono::system_clock::time_point postprocessEnd;
};

class Record
{
public:
	virtual ~Record(){}
    // avoid recursive dependency in shared_ptr
    Analyzer *analyzer;
    std::shared_ptr<TimePoints> timePoints;
};

/* chain  head or tear info */
class Chain_Record : public Record{
public:
	int64_t s_num;
    std::chrono::system_clock::time_point chain_start;
    std::chrono::system_clock::time_point chain_end;
};

class Analyzer
{
private:
    Client *client;
    
public:
    Analyzer();

    Client *getClient();
    void setClient(Client *clt);

    virtual void init() {}
    virtual void start(const std::chrono::system_clock::time_point &beginTime) {}
    virtual void stop(const std::chrono::system_clock::time_point &endTime) {}
    
    std::shared_ptr<Record> produceRecordWrapper();
    virtual std::shared_ptr<Record> produceRecord() = 0;
    virtual void consumeRecord(std::shared_ptr<Record> record) {}
    virtual Json::Value generateReport() = 0;

    virtual void onPreprocess(std::shared_ptr<Record> record) {}
    virtual void onInfer(std::shared_ptr<Record> record) {}
    virtual void onPostprocess(std::shared_ptr<Record> record) {}
};

/* [(chain_id, {(serial_num, record_time),...}),...] */
using chain_map = std::vector<std::pair<int32_t, std::map<int64_t, std::chrono::system_clock::time_point>>>;
using pair_map = std::pair<int32_t, std::map<int64_t, std::chrono::system_clock::time_point>>;

class BasicAnalyzer: public Analyzer
{
private:
    std::chrono::nanoseconds standAloneLatency;

	/* Chain info */
	bool is_chain_head = false;
	bool is_chain_tear = false;
	std::vector<int32_t> chain_ids;
	std::vector<std::string> chain_names;

    std::list<std::shared_ptr<Record>> records;
    std::chrono::system_clock::time_point beginTime;
    std::chrono::system_clock::time_point endTime;

public:
	void set_chain_element(bool _is_chain_head, bool _is_chain_tear, std::vector<int32_t>& chains_info, std::vector<std::string>& chain_names_);
	void set_chain_head_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num);
	void set_chain_tear_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num);

    void setStandAloneLatency(std::chrono::nanoseconds standAloneLatency);
	bool is_chain_head_inf();
	bool is_chain_tear_inf();
	std::shared_ptr<chain_map> get_chain_head_record();
	std::shared_ptr<chain_map> get_chain_tear_record();
	
    virtual void start(const std::chrono::system_clock::time_point &beginTime) override;
    virtual void stop(const std::chrono::system_clock::time_point &endTime) override;
    
    virtual std::shared_ptr<Record> produceRecord() override;
    virtual void consumeRecord(std::shared_ptr<Record> record) override;
    virtual Json::Value generateReport() override;
};


} // namespace DISB

#endif
