#include "client.h"
#include "analyzer.h"
#include "disb_utils.h"

#include <list>
#include <iostream>
#include <chrono>
#include <memory>
#include <fstream>
#include <jsoncpp/json/json.h>
#include <vector>

namespace DISB {

using std::chrono::system_clock;
using std::chrono::nanoseconds;
using std::chrono::microseconds;

Analyzer::Analyzer(): client(nullptr)
{

}

Client *Analyzer::getClient()
{
    return client;
}

void Analyzer::setClient(Client *clt)
{
    client = clt;
}

std::shared_ptr<Record> Analyzer::produceRecordWrapper()
{
    auto record = produceRecord();
    record->analyzer = this;
    return record;
}

void BasicAnalyzer::set_chain_element(bool _is_chain_head, bool _is_chain_tear, std::vector<int32_t>& chains_info, std::vector<std::string>& chain_names_){
	if(_is_chain_head) is_chain_head = true;
	if(_is_chain_tear) is_chain_tear = true;
	
	chain_ids = chains_info;	
	chain_names = chain_names_;
}

void BasicAnalyzer:: set_chain_head_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num){
	if(is_chain_head == true)
		chain_record->chain_start = now_time;
	chain_record->s_num = s_num;
}

void BasicAnalyzer:: set_chain_tear_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num){
	if(is_chain_tear == true)
		chain_record->chain_end = now_time;
	chain_record->s_num = s_num;
}

void BasicAnalyzer::setStandAloneLatency(std::chrono::nanoseconds _standAloneLatency)
{
    standAloneLatency = _standAloneLatency;
}

bool BasicAnalyzer::is_chain_head_inf(){
	return is_chain_head;
}

bool BasicAnalyzer::is_chain_tear_inf(){
	return is_chain_tear;
}

std::shared_ptr<chain_map> BasicAnalyzer::get_chain_head_record(){
	if(!is_chain_head){
        std::cerr << "[Error]: It isn't chain head\n";
        exit(-1);
	}

	std::shared_ptr<chain_map> r_data =  std::make_shared<chain_map>();
	for(auto id : chain_ids){
		pair_map tmp_pair;
		tmp_pair.first = id;
		for(auto record : records){
			std::shared_ptr<Chain_Record> chain_record = std::dynamic_pointer_cast<Chain_Record>(record);
			tmp_pair.second[chain_record->s_num] = chain_record->chain_start;
		}
		r_data->push_back(tmp_pair);
	}

	return r_data;
}

std::shared_ptr<chain_map> BasicAnalyzer::get_chain_tear_record(){
	if(!is_chain_tear){
        std::cerr << "[Error]: It isn't chain tear\n";
        exit(-1);
	}

	std::shared_ptr<chain_map> r_data =  std::make_shared<chain_map>();
	for(auto id : chain_ids){
		pair_map tmp_pair;
		tmp_pair.first = id;
		for(auto record : records){
			std::shared_ptr<Chain_Record> chain_record = std::dynamic_pointer_cast<Chain_Record>(record);
			tmp_pair.second[chain_record->s_num] = chain_record->chain_end;
		}
		r_data->push_back(tmp_pair);
	}

	return r_data;
}

void BasicAnalyzer::start(const system_clock::time_point &_beginTime)
{
    records.clear();
    beginTime = _beginTime;
}

void BasicAnalyzer::stop(const system_clock::time_point &_endTime)
{
    endTime = _endTime;
}

std::shared_ptr<Record> BasicAnalyzer::produceRecord()
{
	if(is_chain_head || is_chain_tear)
		return std::make_shared<Chain_Record>();
	else return std::make_shared<Record>();
}

void BasicAnalyzer::consumeRecord(std::shared_ptr<Record> record)
{
    records.push_back(record);
}

Json::Value BasicAnalyzer::generateReport()
{
    // req/s
    double avgThroughput(0);

    nanoseconds preprocessLatencyAvg(0);
    nanoseconds inferLatencyAvg(0);
    nanoseconds postprocessLatencyAvg(0);
    nanoseconds totalLatencyAvg(0);

    if (records.size() > 0) {
        // analysis records and calculate latency & throughput
        nanoseconds preprocessLatencySum(0);
        nanoseconds inferLatencySum(0);
        nanoseconds postprocessLatencySum(0);

        // std::string name = records.front()->analyzer->getClient()->getName();
        // std::ofstream f(std::string("./") + name + ".txt");
        // int inferId = 0;

        for (auto record : records) {
            std::shared_ptr<TimePoints> tps = record->timePoints;

            // f << "infer id: " << inferId++ << "\n";
            // f << "prepare: " << (tps->prepareInputBegin - beginTime).count()/1000 << "us ~ " << (tps->prepareInputEnd - beginTime).count()/1000 << "us\n";
            // f << "preprocess: " << (tps->preprocessBegin - beginTime).count()/1000 << "us ~ " << (tps->preprocessEnd - beginTime).count()/1000 << "us\n";
            // f << "copy: " << (tps->copyInputBegin - beginTime).count()/1000 << "us ~ " << (tps->copyInputEnd - beginTime).count()/1000 << "us\n";
            // f << "infer: " << (tps->inferBegin - beginTime).count()/1000 << "us ~ " << (tps->inferEnd - beginTime).count()/1000 << "us\n";
            // f << "copy: " << (tps->copyOutputBegin - beginTime).count()/1000 << "us ~ " << (tps->copyOutputEnd - beginTime).count()/1000 << "us\n";
            // f << "postprocess: " << (tps->postprocessBegin - beginTime).count()/1000 << "us ~ " << (tps->postprocessEnd - beginTime).count()/1000 << "us\n\n\n";
            
            preprocessLatencySum += tps->preprocessEnd - tps->preprocessBegin;
            inferLatencySum += tps->inferEnd - tps->inferBegin;
            postprocessLatencySum += tps->postprocessEnd - tps->postprocessBegin;
        }

        preprocessLatencyAvg = preprocessLatencySum / records.size();
        inferLatencyAvg = inferLatencySum / records.size();
        postprocessLatencyAvg = postprocessLatencySum / records.size();
        totalLatencyAvg = preprocessLatencyAvg
                        + inferLatencyAvg
                        + postprocessLatencyAvg;

        nanoseconds benchmarkTime(endTime - beginTime);
        if (benchmarkTime.count() > 0) {
            avgThroughput = double(BILLION * records.size()) / benchmarkTime.count();
        }
    }
    
    Json::Value report;
    report["type"] = "basic";
    report["avgThroughput(req/s)"] = avgThroughput;

    // measured in microseconds
    report["standAloneTotalLatency(us)"] = (Json::Value::Int64) standAloneLatency.count() / THOUSAND;
    report["avgPreprocessLatency(us)"] = (Json::Value::Int64) preprocessLatencyAvg.count() / THOUSAND;
    report["avgInferLatency(us)"] = (Json::Value::Int64) inferLatencyAvg.count() / THOUSAND;
    report["avgPostprocessLatency(us)"] = (Json::Value::Int64) postprocessLatencyAvg.count() / THOUSAND;
    report["avgTotalLatency(us)"] = (Json::Value::Int64) totalLatencyAvg.count() / THOUSAND;
    report["avgTotalLatencyIncrease(us)"] = (Json::Value::Int64) (totalLatencyAvg.count() - standAloneLatency.count()) / THOUSAND;

    return report;
}

} // namespace DISB
