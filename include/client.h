#ifndef _DISB_CLIENT_H_
#define _DISB_CLIENT_H_

#include "analyzer.h"

#include <vector>
#include <memory>
#include <string>
#include <chrono>
#include <jsoncpp/json/json.h>

namespace DISB
{

class InferResult
{
public:
    int64_t resultSerialNumber = 0;
    std::string producerTaskId;
    std::string consumerTaskId;
    //std::vector<std::vector<float>> result;
	int64_t result;

	InferResult() = default;
	InferResult(int64_t s_num, int64_t result_):resultSerialNumber(s_num), result(result_){}
};

class Client
{
private:
    std::string name;
	std::string taskId;
    std::shared_ptr<BasicAnalyzer> basicAnalyzer;
    std::vector<std::shared_ptr<Analyzer>> customAnalyzers;

public:
    Client();
    std::string getName();
    void setName(const std::string &name);
	void settaskId(const std::string &name);

    std::vector<std::shared_ptr<Record>> produceRecords();
    Json::Value generateReport();
    void addAnalyzer(std::shared_ptr<Analyzer> customAnalyzer);
    void initAnalyzers();
	void setAnalyzersChainInfo(bool _is_chain_head, bool _is_chain_tear, std::vector<int32_t>& chain_ids, std::vector<std::string>& chain_names);
    void startAnalyzers(const std::chrono::system_clock::time_point &beginTime);
    void stopAnalyzers(const std::chrono::system_clock::time_point &endTime);
    void setStandAloneLatency(std::chrono::nanoseconds standAloneLatency);
	bool is_chain_head_inf();
	bool is_chain_tear_inf();
	std::shared_ptr<chain_map> get_chain_head_record();
	std::shared_ptr<chain_map> get_chain_tear_record();
	void set_chain_head_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num);
	void set_chain_tear_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num);

	/* 重写：请求序号处理 */
	virtual int64_t get_s_num() {return 0;}
	virtual void set_s_num(int64_t num) {}

    virtual void init() {}
    virtual void prepareInput() {}
    virtual void preprocess() {}
    virtual void copyInput() {}
    virtual void infer() {}
    virtual void copyOutput() {}
    virtual void postprocess() {}
    virtual std::shared_ptr<InferResult> produceResult() { return std::make_shared<InferResult>(); };
};

class DependentClient: public Client
{
public:
    virtual void consumePrevResults(const std::map<std::string, std::shared_ptr<InferResult>> &prevResults) {}
    virtual std::map<std::string, std::shared_ptr<InferResult>> produceDummyPrevResults()
    {
        return std::map<std::string, std::shared_ptr<InferResult>>();
    }
};


}

#endif
