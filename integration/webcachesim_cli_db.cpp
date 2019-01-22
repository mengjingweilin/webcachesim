//
// Created by zhenyus on 1/19/19.
//

#include <fstream>
#include <string>
#include <regex>
#include "lru_variants.h"
#include "gd_variants.h"
#include "request.h"
#include "simulation.h"
#include <map>

#include <cstdlib>
#include <iostream>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

using namespace std;
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;



int main (int argc, char* argv[])
{
  // output help if insufficient params
  if(argc < 4) {
    cerr << "webcachesim traceFile cacheType cacheSizeBytes [cacheParams]" << endl;
    return 1;
  }

  map<string, string> params;

  regex opexp ("(.*)=(.*)");
  cmatch opmatch;
  string paramSummary;
  try {
      for (int i = 4; i < argc; i++) {
          if (paramSummary.length() > 0) {
              paramSummary += "-";
          }
          regex_match(argv[i], opmatch, opexp);
          if (opmatch.size() != 3) {
              cerr << "each cacheParam needs to be in form name=value" << endl;
              return 1;
          }
          cerr << opmatch[1] << endl << opmatch[2] << endl;
          params[opmatch[1]] = opmatch[2];
          paramSummary += opmatch[2];
      }
  } catch (const std::regex_error& e) {
      cerr << "regex_error caught: " << e.what() << '\n';
      if (e.code() == std::regex_constants::error_brack) {
          cerr << "The code was error_brack\n";
      }
  }

  auto webcachesim_trace_dir = getenv("WEBCACHESIM_TRACE_DIR");
  if (!webcachesim_trace_dir) {
      cerr<<"error: WEBCACHESIM_TRACE_DIR is not set, can not find trace"<<endl;
      return -1;
  }

  map<string, string> simulation_params;
  for (auto &k: params) {
      if (k.first == "dbcollection" || k.first == "dburl")
        continue;
      else
        simulation_params[k.first] = k.second;
    }

  auto timeBegin = chrono::system_clock::now();
  auto res = simulation(string(webcachesim_trace_dir) + '/' + argv[1], argv[2], std::stoull(argv[3]), simulation_params);
  auto simulation_time = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - timeBegin).count();

  mongocxx::instance inst;

  try {
    mongocxx::client client = mongocxx::client{mongocxx::uri(params["dburl"])};
    mongocxx::database db = client["webcachesim"];
    bsoncxx::builder::basic::document key_builder{};
    bsoncxx::builder::basic::document value_builder{};
    key_builder.append(kvp("trace_file", argv[1]));
    key_builder.append(kvp("cache_type", argv[2]));
    key_builder.append(kvp("cache_size", argv[3]));
    value_builder.append(kvp("trace_file", argv[1]));
    value_builder.append(kvp("cache_type", argv[2]));
    value_builder.append(kvp("cache_size", argv[3]));
    value_builder.append(kvp("simulation_time", to_string(simulation_time)));

    for (auto &k: simulation_params) {
      key_builder.append(kvp(k.first, k.second));
      value_builder.append(kvp(k.first, k.second));
    }

    for (auto &k: res) {
      value_builder.append(kvp(k.first, k.second));
    }

    mongocxx::options::replace option;
    db[params["dbcollection"]].replace_one(key_builder.extract(), value_builder.extract(), option.upsert(true));
    return EXIT_SUCCESS;
  } catch (const std::exception& xcp) {
    cerr << "error: db connection failed: " << xcp.what() << std::endl;
    return EXIT_FAILURE;
  }
}