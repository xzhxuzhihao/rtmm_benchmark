#include "disb.h"
#include "chain_model_client.h"

#include <string>
#include <iostream>
#include <jsoncpp/json/json.h>


int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cout << "Usage: hellodisb config.json" << std::endl;
        return -1;
    }

    DISB::BenchmarkSuite benchmark;
    std::string jsonStr = readStringFromFile(argv[1]);
    benchmark.init(jsonStr, ModelClientFactory);
    benchmark.run();
    Json::Value report = benchmark.generateReport();
    std::cout << report << std::endl;

    return 0;
}

