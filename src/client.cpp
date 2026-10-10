#include "client.h"

#include <vector>
#include <memory>
#include <string>
#include <chrono>
#include <jsoncpp/json/json.h>

namespace DISB
{

using std::chrono::system_clock;

Client::Client():
    name("Unknown"),
    basicAnalyzer(std::make_shared<BasicAnalyzer>())
{
    basicAnalyzer->setClient(this);
}

std::string Client::getName()
{
    return name;
}

void Client::setName(const std::string &_name)
{
    name = _name;
}

void Client::settaskId(const std::string &_name){
	taskId = _name;
}

std::vector<std::shared_ptr<Record>> Client::produceRecords()
{
    std::shared_ptr<TimePoints> timePoints = std::make_shared<TimePoints>();
    std::vector<std::shared_ptr<Record>> records;

    auto record = basicAnalyzer->produceRecordWrapper();
    record->timePoints = timePoints;
    records.push_back(record);

    for (auto customAnalyzer : customAnalyzers) {
        auto customRecord = customAnalyzer->produceRecordWrapper();
        customRecord->timePoints = timePoints;
        records.push_back(customRecord);
    }

    return records;
}

Json::Value Client::generateReport()
{
    Json::Value analyzerReports;
    
    analyzerReports.append(basicAnalyzer->generateReport());
    for (auto customAnalyzer : customAnalyzers) {
        analyzerReports.append(customAnalyzer->generateReport());
    }

    Json::Value report;
    report["task_id"] = taskId;
    report["client"] = name;
    report["analyzers"] = analyzerReports;
    return report;
}

void Client::addAnalyzer(std::shared_ptr<Analyzer> customAnalyzer)
{
    customAnalyzer->setClient(this);
    customAnalyzers.push_back(customAnalyzer);
}

void Client::initAnalyzers()
{
    basicAnalyzer->init();
    for (auto customAnalyzer : customAnalyzers) {
        customAnalyzer->init();
    }
}

void Client::setAnalyzersChainInfo(bool _is_chain_head, bool _is_chain_tear, std::vector<int32_t>& chain_ids, std::vector<std::string>& chain_names){
	basicAnalyzer->set_chain_element(_is_chain_head, _is_chain_tear, chain_ids, chain_names);
}

void Client::startAnalyzers(const std::chrono::system_clock::time_point &beginTime)
{
    basicAnalyzer->start(beginTime);
    for (auto customAnalyzer : customAnalyzers) {
        customAnalyzer->start(beginTime);
    }
}

void Client::stopAnalyzers(const std::chrono::system_clock::time_point &endTime)
{
    basicAnalyzer->stop(endTime);
    for (auto customAnalyzer : customAnalyzers) {
        customAnalyzer->stop(endTime);
    }
}

void Client::setStandAloneLatency(std::chrono::nanoseconds standAloneLatency)
{
    basicAnalyzer->setStandAloneLatency(standAloneLatency);
}

bool Client::is_chain_head_inf(){
	return basicAnalyzer->is_chain_head_inf();
}

bool Client::is_chain_tear_inf(){
	return basicAnalyzer->is_chain_tear_inf();
}

std::shared_ptr<chain_map> Client::get_chain_head_record(){
	return basicAnalyzer->get_chain_head_record();
}

std::shared_ptr<chain_map> Client::get_chain_tear_record(){
	return basicAnalyzer->get_chain_tear_record();
}

void Client::set_chain_head_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num){
	basicAnalyzer->set_chain_head_records(now_time, chain_record, s_num);
}

void Client::set_chain_tear_records(std::chrono::system_clock::time_point now_time, std::shared_ptr<Chain_Record> chain_record, int64_t s_num){
	basicAnalyzer->set_chain_tear_records(now_time, chain_record, s_num);
}

} // namespace DISB
