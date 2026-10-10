#include "benchmark.h"
#include "disb_utils.h"

#include <algorithm>
#include <jsoncpp/json/value.h>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <cassert>
#include <iostream>
#include <jsoncpp/json/json.h>

namespace DISB
{

using std::chrono::system_clock;
using std::chrono::seconds;
using std::chrono::microseconds;
using std::chrono::nanoseconds;

BenchmarkTask::BenchmarkTask(std::string taskId, std::shared_ptr<Client> clt, std::shared_ptr<Load> stg):
    id(taskId), client(clt), load(stg), standAloneLatency(std::make_shared<StandAloneLatency>())
{
    std::shared_ptr<DependentLoad> dependentLoad = std::dynamic_pointer_cast<DependentLoad>(load);
    if (dependentLoad == nullptr) return;

    isDependent = true;
    std::shared_ptr<DependentClient> dependentClient = std::dynamic_pointer_cast<DependentClient>(client);

    if (dependentClient == nullptr) {
        std::cerr << "[Error]: Client " << id << " has a dependent load, it should inherit DISB::DependentClient\n";
        exit(-1);
    }

    // check every dependent task should have at least one previous task
    if (dependentLoad->getPrevTaskIds().size() == 0) {
        std::cerr << "[Error]: DependentLoad of Client " << id << " has no dependencies\n";
        exit(-1);
    }
}

void BenchmarkTask::inferOnce()
{
    if (isDependent) {
        std::shared_ptr<DependentLoad> dependentLoad = std::dynamic_pointer_cast<DependentLoad>(load);
        std::shared_ptr<DependentClient> dependentClient = std::dynamic_pointer_cast<DependentClient>(client);
        assert(dependentLoad);
        assert(dependentClient);
		const std::map<std::string, std::shared_ptr<InferResult>> &prevResults = dependentLoad->getPrevResults();
		std::shared_ptr<InferResult> pre_one_result = prevResults.begin()->second;
		int64_t s_num_dep = pre_one_result->resultSerialNumber;
		client->set_s_num(s_num_dep);
	
        dependentClient->consumePrevResults(prevResults);
    }else{
		int64_t s_num_pre = client->get_s_num();
		client->set_s_num(s_num_pre+1);
	}

    std::vector<std::shared_ptr<Record>> records = client->produceRecords();

	if(std::dynamic_pointer_cast<Chain_Record>(records.front()) != nullptr){
		std::shared_ptr<Chain_Record> chain_record = std::dynamic_pointer_cast<Chain_Record>(records.front());
		std::chrono::system_clock::time_point now_time = system_clock::now();
		client->set_chain_head_records(now_time, chain_record, client->get_s_num());	
	}

    // records should always be not empty
    // because a basic analyzer will always be there, producing at least one record
    // for safety's sake, here will check records' length
    std::shared_ptr<TimePoints> timePoints = records.empty()
                                             ? std::make_shared<TimePoints>()
                                             : records.front()->timePoints;

    // trigger analyers' preprocess event and record execution time
    for (auto record : records) record->analyzer->onPreprocess(record);
    timePoints->preprocessBegin = system_clock::now();
    client->preprocess();
    timePoints->preprocessEnd = system_clock::now();

    // trigger analyers' infer event and record execution time
    for (auto record : records) record->analyzer->onInfer(record);
    timePoints->inferBegin = system_clock::now();
    client->infer();
    timePoints->inferEnd = system_clock::now();

    // trigger analyers' postprocess event and record execution time
    for (auto record : records) record->analyzer->onPostprocess(record);
    timePoints->postprocessBegin = system_clock::now();
    client->postprocess();
    timePoints->postprocessEnd = system_clock::now();

	if(std::dynamic_pointer_cast<Chain_Record>(records.front()) != nullptr){
		std::shared_ptr<Chain_Record> chain_record = std::dynamic_pointer_cast<Chain_Record>(records.front());
		std::chrono::system_clock::time_point now_time = system_clock::now();
		client->set_chain_tear_records(now_time, chain_record, client->get_s_num());	
	}

    // give the record back to the analyzer produced it
    for (auto record : records) record->analyzer->consumeRecord(record);

    std::shared_ptr<InferResult> inferResult = client->produceResult();
    inferResult->producerTaskId = this->id;

    for (auto nextTask : nextTasks) {
        std::shared_ptr<DependentLoad> dependentLoad = std::dynamic_pointer_cast<DependentLoad>(nextTask->load);
        if (dependentLoad == nullptr) {
            continue;
        }
        inferResult->consumerTaskId = nextTask->id;
        dependentLoad->recvResult(inferResult);
    }
}

void BenchmarkTask::runBenchmark(const system_clock::time_point &beginTime,
                                 const system_clock::time_point &endTime)
{
    load->start(beginTime, endTime);
    client->startAnalyzers(beginTime);
    
    while (load->waitUntilNextLaunch()) {
        inferOnce();
    }

    load->stop();
    client->stopAnalyzers(endTime);
}

BenchmarkSuite::BenchmarkSuite()
{

}

BenchmarkSuite::~BenchmarkSuite()
{

}

void BenchmarkSuite::init(const std::string &configJsonStr,
						  std::shared_ptr<DISB::Client> clientFactory(const Json::Value &config, const Json::Value &config_extra),
                          std::shared_ptr<Load> loadFactory(const Json::Value &config))
{
    Json::Reader reader;
    Json::Value configJson;

    reader.parse(configJsonStr, configJson, false);
    init(configJson, clientFactory, loadFactory);
}

void BenchmarkSuite::init(const Json::Value &configJson,
						  std::shared_ptr<DISB::Client> clientFactory(const Json::Value &config, const Json::Value &config_extra),
                          std::shared_ptr<Load> loadFactory(const Json::Value &config))
{
    benchmarkTime = seconds(configJson["time"].asInt());
	std::cout << "测试时间：" << benchmarkTime.count() << "s" << std::endl;
    for (const Json::Value &taskConfig : configJson["tasks"]) {
        std::string taskId = taskConfig["id"].asString();
        std::shared_ptr<Client> client = clientFactory(taskConfig["client"], taskConfig["chain"][0]);
        std::shared_ptr<Load> load = loadFactory(taskConfig["load"]);
		bool is_head = taskConfig["chain"][0]["is_head"].asBool();
		bool is_tear = taskConfig["chain"][0]["is_tear"].asBool();
		std::vector<int32_t> chain_ids;
		std::vector<std::string> chain_names;
		for (const Json::Value &ChainConfig : taskConfig["chain"]) {
			int32_t chain_id = ChainConfig["chain_id"].asInt();
			std::string chain_name = ChainConfig["chain_name"].asString();
			chain_ids.push_back(chain_id);
			chain_names.push_back(chain_name);
		}
		/* 链式信息全部初始化完成（链名、链号、节点类型）*/
		client->setAnalyzersChainInfo(is_head, is_tear, chain_ids, chain_names);
		client->settaskId(taskId);

        // check if the factory method is correct
        if (client != nullptr && std::dynamic_pointer_cast<Client>(client) != nullptr
            && load != nullptr && std::dynamic_pointer_cast<Load>(load) != nullptr) {
            client->init();
            client->initAnalyzers();
            load->init();
            tasks[taskId] = BenchmarkTask(taskId, client, load);
        }

		std::cout << "chain names：" << std::endl;
		for(auto &t : chain_names){
			std::cout << "       " << t << std::endl;
		}
		std::cout << "task id：" << taskId << " 初始化完成\n" << std::endl;
    }

    for (auto &taskPair : tasks) {
        auto &task = taskPair.second;
        std::shared_ptr<DependentLoad> dependentLoad = std::dynamic_pointer_cast<DependentLoad>(task.load);
        if (dependentLoad == nullptr) continue;

        for (auto &prevTaskId : dependentLoad->getPrevTaskIds()) {
            if (tasks.find(prevTaskId) == tasks.end()) {
                std::cerr << "[Error]: Task ID " << prevTaskId << " NOT FOUND\n";
                exit(-1);
            }
            tasks[prevTaskId].nextTasks.push_back(&task);
        }
    }
}

void BenchmarkSuite::run(void loadCoordinator(const std::vector<LoadInfo> &loadInfos))
{
    // 1. warm up
    for (auto &taskPair : tasks) {
        auto &task = taskPair.second;

        if (task.isDependent) {
            std::shared_ptr<DependentClient> dependentClient = std::dynamic_pointer_cast<DependentClient>(task.client);
            assert(dependentClient);
            dependentClient->consumePrevResults(dependentClient->produceDummyPrevResults());
        }

        for (int i = 0; i < WARMUP_TIME; ++ i) {
            task.client->preprocess();
            task.client->infer();
            task.client->postprocess();
        }
		std::cout << task.id << " 热启动完成" << std::endl;
    }

    // 2. measure the stand alone latency of each client
    for (auto &taskPair : tasks) {
        auto &task = taskPair.second;
        // sleep 100 ms to reduce influence between two tasks
        std::this_thread::sleep_for(microseconds(100000));

        nanoseconds preprocessLatencySum(0);
        nanoseconds inferLatencySum(0);
        nanoseconds postprocessLatencySum(0);

        for (int i = 0; i < STANDALONE_LATENCY_MEASURE_TIME; ++i) {
            if (task.isDependent) {
                std::shared_ptr<DependentClient> dependentClient = std::dynamic_pointer_cast<DependentClient>(task.client);
                assert(dependentClient);
                dependentClient->consumePrevResults(dependentClient->produceDummyPrevResults());
            }
        
            auto begin = system_clock::now();
            task.client->preprocess();
            auto end = system_clock::now();
            preprocessLatencySum += end - begin;

            begin = system_clock::now();
            task.client->infer();
            end = system_clock::now();
            inferLatencySum += end - begin;

            begin = system_clock::now();
            task.client->postprocess();
            end = system_clock::now();
            postprocessLatencySum += end - begin;
        }

        task.standAloneLatency->preprocessLatency = preprocessLatencySum / STANDALONE_LATENCY_MEASURE_TIME;
        task.standAloneLatency->inferLatency = inferLatencySum / STANDALONE_LATENCY_MEASURE_TIME;
        task.standAloneLatency->postprocessLatency = postprocessLatencySum / STANDALONE_LATENCY_MEASURE_TIME;

        task.client->setStandAloneLatency(task.standAloneLatency->sum());

		std::cout << task.id << " 单模型基准测试完成" << std::endl;
    }

    // 3. coordinate
    std::vector<LoadInfo> loadInfos;
    for (auto &taskPair : tasks) {
        auto &task = taskPair.second;
        loadInfos.emplace_back(task.load, task.standAloneLatency);
    }
    loadCoordinator(loadInfos);

    // 4. launch benchmark
    beginTime = system_clock::now() + seconds(DELAY_BEGIN_SECONDS);
    endTime = beginTime + seconds(benchmarkTime);

    std::vector<std::thread> benchmarkThreads;
    
    for (auto &taskPair : tasks) {
        auto &task = taskPair.second;
        benchmarkThreads.emplace_back(&BenchmarkTask::runBenchmark, task, beginTime, endTime);
		std::cout << task.id << " 任务线程创建完成" << std::endl;
    }

	std::cout <<  "线程id分别为：" << std::endl;
    for (auto &thread : benchmarkThreads){
		auto old_flags = std::cout.flags();
	    std::cout << std::hex << std::showbase<< thread.get_id() << '\n';
		// 3. 恢复原始状态（恢复为默认的 std::dec 十进制等）
		std::cout.flags(old_flags);
	}

    for (auto &thread : benchmarkThreads) {
        thread.join();
	    std::cout << log_timestamp() << "线程运行结束"  << '\n';
    }
}

Json::Value BenchmarkSuite::getChainRecordReport(){
    Json::Value report;

	//report["chain_all_data"]
	std::map<int32_t, pair_map*> head_records;
	std::vector<pair_map*> tear_records;

	std::vector<std::pair<int32_t, std::vector<Json::Value::Int64>>> processed_all_records;
	/* time unit: us */
	std::vector<std::pair<int32_t, Json::Value::Int64>> processed_avg_records;
	std::vector<std::pair<int32_t, Json::Value::Int64>> processed_p99_records;
	std::vector<std::pair<int32_t, Json::Value::Int64>> chain_throughputs;

	std::vector<std::shared_ptr<chain_map>> chain_map_holder;
    for (auto &taskPair : tasks) {
		std::shared_ptr<Client> tmp_client = taskPair.second.client;
		if(tmp_client->is_chain_head_inf()){
			auto chain_map_tmp = tmp_client->get_chain_head_record();
			chain_map_holder.push_back(chain_map_tmp);
			for(auto &pair : *chain_map_tmp){
				head_records[pair.first] = &pair;
			}
		}
		if(tmp_client->is_chain_tear_inf()){
			auto chain_map_tmp = tmp_client->get_chain_tear_record();
			chain_map_holder.push_back(chain_map_tmp);
			for(auto &pair : *chain_map_tmp){
				tear_records.push_back(&pair);
			}
		}
    }

	for(auto pair : tear_records){
		std::vector<Json::Value::Int64> tmp_array;
		int32_t id = pair->first;
		for(auto pair_m : pair->second){
			int64_t s_num = pair_m.first;
			
			if(head_records[id]->second.find(s_num) != head_records[id]->second.end()){
				nanoseconds tmp_time = pair_m.second - head_records[id]->second[s_num];
				tmp_array.push_back((Json::Value::Int64)tmp_time.count()/THOUSAND);
			}
		}
        double avgThroughput = double(pair->second.size()) / benchmarkTime.count();
		chain_throughputs.push_back({id, avgThroughput});
		processed_all_records.push_back({id, tmp_array});
	}

	for(auto &pair : processed_all_records){
		Json::Value::Int64 avg_time;	
		Json::Value::Int64 sum_time = 0;
		for(auto data : pair.second){
			sum_time += data;
		}
		avg_time = sum_time/pair.second.size();
		processed_avg_records.push_back({pair.first, avg_time});
	}

	for (auto &record_pair : processed_all_records) {
		int32_t id = record_pair.first;
		auto &latencies = record_pair.second; // 获取时延数组

		// 1. 边界检查：如果该 ID 没有采集到任何数据，赋 0
		if (latencies.empty()) {
			processed_p99_records.push_back({id, 0});
			continue;
		}

		// 2. 计算 P99 对应的下标 (百分位 nearest rank 公式)
		size_t p99_idx = static_cast<size_t>(std::ceil(latencies.size() * 0.99)) - 1;
		if (p99_idx >= latencies.size()) {
			p99_idx = latencies.size() - 1;
		}

		// 3. 提取 P99 值（推荐使用 std::nth_element，平均时间复杂度 O(N)，比全排序 std::sort 更高效）
		std::nth_element(latencies.begin(), latencies.begin() + p99_idx, latencies.end());
		Json::Value::Int64 p99_val = latencies[p99_idx];

		// 4. 保存计算结果
		processed_p99_records.push_back({id, p99_val});
	}

    Json::Value report_all_data;
	report_all_data["type"] = "chain_all_time_data";

	for(auto &pair : processed_all_records){
		Json::Value report_all_tmp;
		report_all_tmp["chain_id"] = pair.first;
		Json::Value report_tmp;
		for(auto data : pair.second){
			report_tmp.append(data);
		}
		report_all_tmp["time(us)"] = report_tmp;
		report_all_data["chain_data"].append(report_all_tmp);
	}

	report.append(report_all_data);

    Json::Value report_avg_data;
	report_avg_data["type"] = "chain_avg_time_data";

	for(auto &pair : processed_avg_records){
		Json::Value report_avg_tmp;
		report_avg_tmp["chain_id"] = pair.first;
		report_avg_tmp["time(us)"] = pair.second;
		report_avg_data["chain_data"].append(report_avg_tmp);
	}

	report.append(report_avg_data);

    Json::Value report_p99_data;
	report_p99_data["type"] = "chain_p99_time_data";

	for(auto &pair : processed_p99_records){
		Json::Value report_p99_tmp;
		report_p99_tmp["chain_id"] = pair.first;
		report_p99_tmp["time(us)"] = pair.second;
		report_p99_data["chain_data"].append(report_p99_tmp);
	}

	report.append(report_p99_data);

    Json::Value report_throughput_data;
	report_throughput_data["type"] = "chain_throughput_data";
	for(auto &pair : chain_throughputs){
		Json::Value report_throughput_tmp;
		report_throughput_tmp["chain_id"] = pair.first;
		report_throughput_tmp["Throughput"] = pair.second;
		report_throughput_data["chain_data"].append(report_throughput_tmp);
	}

	report.append(report_throughput_data);

	return report;
}

Json::Value BenchmarkSuite::generateReport()
{
    Json::Value report;
    report["beginTime"] = timepointToString(beginTime);
    report["endTime"] = timepointToString(endTime);
    report["benchmarkTime(s)"] = (Json::Value::Int64) benchmarkTime.count();

    Json::Value results;
    for (auto &taskPair : tasks) {
        results.append(taskPair.second.client->generateReport());
    }
    report["model_results"] = results;

	report["chain_result"] = getChainRecordReport();

    return report;
}

}
