//
// Created by zhenyus on 11/8/18.
//

#include "random_variants.h"
#include <algorithm>
#include "utils.h"

using namespace std;

//init with a wrong value
uint8_t LRMeta::_n_past_intervals = 0;
uint8_t Gradient::_n_past_intervals = 0;

bool RandomCache::lookup(SimpleRequest &req) {
    return key_space.exist(req._id);
}

void RandomCache::admit(SimpleRequest &req) {
    const uint64_t & size = req._size;
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req.get_id(), size);
        return;
    }
    // admit new object
    key_space.insert(req._id);
    object_size.insert({req._id, req._size});
    _currentSize += size;

    // check eviction needed
    while (_currentSize > _cacheSize) {
        evict();
    }
}

void RandomCache::evict() {
    auto key = key_space.pickRandom();
    key_space.erase(key);
    auto & size = object_size.find(key)->second;
    _currentSize -= size;
    object_size.erase(key);
}



void LRCache::try_train(uint64_t &t) {
    static uint64_t next_idx = 0;
    if (t < gradient_window)
        return;
    //look at previous window
    auto gradient_window_idx = t / gradient_window - 1;
    //already update
    if (gradient_window_idx != next_idx)
        return;
    ++next_idx;
    //perhaps no gradient at all
    if (gradient_window_idx >= pending_gradients.size())
        return;

    if (bias_point == none) {
        uint64_t n_update = 0;
        for (uint j = 0; j < n_window_bins && !pending_gradients[gradient_window_idx].empty(); ++j) {
            auto &gradients = pending_gradients[gradient_window_idx][j];
            if (gradients.n_update) {
                n_update += gradients.n_update;
            }
        }
        if (!n_update)
            return;
        for (uint j = 0; j < n_window_bins && !pending_gradients[gradient_window_idx].empty(); ++j) {
            auto &gradients = pending_gradients[gradient_window_idx][j];
            if (gradients.n_update) {
                bias -= learning_rate / n_update * gradients.bias;
                for (uint i = 0; i < n_past_intervals; ++i)
                    weights[i] -= learning_rate / n_update * gradients.weights[i];
            }
        }
        return;
    }

    if ( edge <= bias_point && bias_point <= mid ) {
        //bias weight
        double f_evicted_idx = ((double) (* f_evicted)) / size_bin;
        auto sample_bias = vector<double >(n_window_bins);
        double sum_sample_bias = 0;
        for (uint j = 0; j < n_window_bins && ! pending_gradients[gradient_window_idx].empty(); ++j) {
            auto &gradients = pending_gradients[gradient_window_idx][j];
            double class_id;  //smaller is better
            double base;
            if (gradients.n_update) {
                if (bias_point == edge) {
                    if (j <= f_evicted_idx) {
                        class_id = j;
                    } else {
                        class_id = n_window_bins - 1 - j;
                    }
                } else if (bias_point == center) {
                    class_id = abs(f_evicted_idx - j);
                } else if (bias_point == mid) {
                    if (j <= f_evicted_idx) {
                        class_id = abs(f_evicted_idx / 2. - j);
                    } else {
                        class_id = abs((f_evicted_idx + n_window_bins) / 2 - j);
                    }
                }
                base = 1 - 0.1 * class_id;
                double weight = pow(base, alpha);
                sample_bias[j] = weight * gradients.n_update;
                sum_sample_bias += weight * gradients.n_update;
            }
        }

        if (sum_sample_bias == 0)
            return;

        for (uint j = 0; j < n_window_bins && ! pending_gradients[gradient_window_idx].empty(); ++j) {
            auto &gradients = pending_gradients[gradient_window_idx][j];
            if (gradients.n_update) {
                if (gradients.n_update) {
                    bias -= learning_rate / gradients.n_update / sum_sample_bias * sample_bias[j] * gradients.bias ;
                    for (uint i = 0; i < n_past_intervals; ++i)
                        weights[i] -= learning_rate / gradients.n_update / sum_sample_bias * sample_bias[j] * gradients.weights[i];
                }
            }
        }
        return;
    }

    if (bias_point == rebalance) {
        uint64_t n_update = 0;
        for (uint j = 0; j < n_window_bins && !pending_gradients[gradient_window_idx].empty(); ++j) {
            auto &gradients = pending_gradients[gradient_window_idx][j];
            if (gradients.n_update) {
                n_update += 1;
            }
        }
        if (!n_update)
            return;
        for (uint j = 0; j < n_window_bins && !pending_gradients[gradient_window_idx].empty(); ++j) {
            auto &gradients = pending_gradients[gradient_window_idx][j];
            if (gradients.n_update) {
                bias -= learning_rate / gradients.n_update / n_update * gradients.bias;
                for (uint i = 0; i < n_past_intervals; ++i)
                    weights[i] -= learning_rate / gradients.n_update / n_update * gradients.weights[i];
            }
        }
        return;
    }

    if (bias_point == bin0) {
        if(!pending_gradients[gradient_window_idx].empty()) {
            auto &gradients = pending_gradients[gradient_window_idx][0];
            if (gradients.n_update) {
                bias -= learning_rate / gradients.n_update * gradients.bias;
                for (uint i = 0; i < n_past_intervals; ++i)
                    weights[i] -= learning_rate / gradients.n_update * gradients.weights[i];
            }
        }
        return;
    }

    if (bias_point == sides) {
        uint64_t n_update = 0;
        double f_evicted_idx = ((double) (* f_evicted)) / size_bin;
        for (uint j = 0; j < n_window_bins && !pending_gradients[gradient_window_idx].empty(); ++j) {
            if ((f_evicted_idx - j < 2 && f_evicted_idx - j >= 1 ) || (j > f_evicted_idx && j - f_evicted_idx <=1)) {
                auto &gradients = pending_gradients[gradient_window_idx][j];
                if (gradients.n_update) {
                    n_update += gradients.n_update;
                }
            }
        }
        if (!n_update)
            return;
        for (uint j = 0; j < n_window_bins && !pending_gradients[gradient_window_idx].empty(); ++j) {
            if ((f_evicted_idx - j < 2 && f_evicted_idx - j >= 1 ) || (j > f_evicted_idx && j - f_evicted_idx <=1)) {
                auto &gradients = pending_gradients[gradient_window_idx][j];
                if (gradients.n_update) {
                    bias -= learning_rate / n_update * gradients.bias;
                    for (uint i = 0; i < n_past_intervals; ++i)
                        weights[i] -= learning_rate / n_update * gradients.weights[i];
                }
            }
        }
        return;
    }

}

void LRCache::sample(uint64_t &t) {
    if (meta_holder[0].empty() || meta_holder[1].empty())
        return;
#ifdef LOG_SAMPLE_RATE
    bool log_flag = ((double) rand() / (RAND_MAX)) < LOG_SAMPLE_RATE;
#endif

    //sample list 0
    {
        uint32_t rand_idx = _distribution(_generator) % meta_holder[0].size();
        uint n_sample = min( (uint) ceil((double) sample_rate*meta_holder[0].size()/(meta_holder[0].size()+meta_holder[1].size())),
                             (uint) meta_holder[0].size());

        for (uint32_t i = 0; i < n_sample; i++) {
            uint32_t pos = (i + rand_idx) % meta_holder[0].size();
            auto &meta = meta_holder[0][pos];

            //fill in past_interval
            uint8_t j = 0;
            auto past_intervals = vector<double >(n_past_intervals);
            for (j = 0; j < meta._past_timestamp_idx && j < n_past_intervals; ++j) {
                uint8_t past_timestamp_idx = (meta._past_timestamp_idx - 1 - j) % n_past_intervals;
                uint64_t past_interval = t - meta._past_timestamps[past_timestamp_idx];
                if (past_interval >= threshold)
                    past_intervals[j] = log1p_threshold;
                else
                    past_intervals[j] = log1p(past_interval);
            }
            for (; j < n_past_intervals; j++)
                past_intervals[j] = log1p_threshold;


            uint64_t known_future_interval;
            double log1p_known_future_interval;
            //known_future_interval < threshold
            if (meta._future_timestamp - t < threshold) {
                known_future_interval = meta._future_timestamp - t;
                log1p_known_future_interval = log1p(known_future_interval);
            }
            else {
                known_future_interval = threshold-1;
                log1p_known_future_interval = log1p_threshold;
            }

#ifdef LOG_SAMPLE_RATE
            //print distribution
            if (log_flag) {
                cout << 0 <<" ";
                for (uint k = 0; k < n_past_intervals; ++k)
                    cout << past_intervals[k] << " ";
                cout << log1p_known_future_interval << endl;
            }
#endif

            double score = 0;
            for (j = 0; j < n_past_intervals; j++)
                score += weights[j] * past_intervals[j];

            //statistics
            double diff = score + bias - log1p_known_future_interval;
            mean_diff = 0.99 * mean_diff + 0.01 * abs(diff);

            //update gradient
            auto gradient_window_idx = (t + known_future_interval) / gradient_window;
            auto bin_idx = known_future_interval / size_bin;
            if (gradient_window_idx >= pending_gradients.size())
                pending_gradients.resize(gradient_window_idx + 1);
            if (pending_gradients[gradient_window_idx].empty())
                pending_gradients[gradient_window_idx].resize(n_window_bins);
            auto &gradient = pending_gradients[gradient_window_idx][bin_idx];
            for (uint k = 0; k < n_past_intervals; ++k)
                gradient.weights[k] += diff * past_intervals[k];
            gradient.bias += diff;
            ++gradient.n_update;
        }
    }

//    cout<<n_out_window<<endl;

    //sample list 1
    if (meta_holder[1].size()){
        uint32_t rand_idx = _distribution(_generator) % meta_holder[1].size();
        //sample less from list 1 as there are gc
        uint n_sample = min( (uint) floor( (double) sample_rate*meta_holder[1].size()/(meta_holder[0].size()+meta_holder[1].size())),
                             (uint) meta_holder[1].size());
//        cout<<n_sample<<endl;

        for (uint32_t i = 0; i < n_sample; i++) {
            //garbage collection
            while (meta_holder[1].size()) {
                uint32_t pos = (i + rand_idx) % meta_holder[1].size();
                auto &meta = meta_holder[1][pos];
                uint8_t oldest_idx = (meta._past_timestamp_idx - (uint8_t) 1)%n_past_intervals;
                uint64_t & past_timestamp = meta._past_timestamps[oldest_idx];
                if (past_timestamp + threshold < t) {
                    uint64_t & ekey = meta._key;
                    key_map.erase(ekey);
                    //evict
                    uint32_t tail1_pos = meta_holder[1].size()-1;
                    if (pos !=  tail1_pos) {
                        //swap tail
                        meta_holder[1][pos] = meta_holder[1][tail1_pos];
                        key_map.find(meta_holder[1][tail1_pos]._key)->second.second = pos;
                    }
                    meta_holder[1].pop_back();
                } else
                    break;
            }
            if (!meta_holder[1].size())
                break;
            uint32_t pos = (i + rand_idx) % meta_holder[1].size();
            auto &meta = meta_holder[1][pos];
            //fill in past_interval
            uint8_t j = 0;
            auto past_intervals = vector<double >(n_past_intervals);
            for (j = 0; j < meta._past_timestamp_idx && j < n_past_intervals; ++j) {
                uint8_t past_timestamp_idx = (meta._past_timestamp_idx - 1 - j) % n_past_intervals;
                uint64_t past_interval = t - meta._past_timestamps[past_timestamp_idx];
                if (past_interval >= threshold)
                    past_intervals[j] = log1p_threshold;
                else
                    past_intervals[j] = log1p(past_interval);
            }
            for (; j < n_past_intervals; j++)
                past_intervals[j] = log1p_threshold;

            uint64_t known_future_interval;
            double log1p_known_future_interval;
            if (meta._future_timestamp - t < threshold) {
                known_future_interval = meta._future_timestamp - t;
                log1p_known_future_interval = log1p(known_future_interval);
            }
            else {
                known_future_interval = threshold - 1;
                log1p_known_future_interval = log1p_threshold;
            }

#ifdef LOG_SAMPLE_RATE
            //print distribution
            if (log_flag) {
                cout << 1 <<" ";
                for (uint k = 0; k < n_past_intervals; ++k)
                    cout << past_intervals[k] << " ";
                cout << log1p_known_future_interval << endl;
            }
#endif
            double score = 0;
            for (j = 0; j < n_past_intervals; j++)
                score += weights[j] * past_intervals[j];

            //statistics
            double diff = score + bias - log1p_known_future_interval;
            mean_diff = 0.99 * mean_diff + 0.01 * abs(diff);


            //update gradient
            auto gradient_window_idx = (t + known_future_interval) / gradient_window;
            auto bin_idx = known_future_interval / size_bin;
            if (gradient_window_idx >= pending_gradients.size())
                pending_gradients.resize(gradient_window_idx + 1);
            if (pending_gradients[gradient_window_idx].empty())
                pending_gradients[gradient_window_idx].resize(n_window_bins);
            auto &gradient = pending_gradients[gradient_window_idx][bin_idx];
            for (uint k = 0; k < n_past_intervals; ++k)
                gradient.weights[k] += diff * past_intervals[k];
            gradient.bias += diff;
            ++gradient.n_update;
        }
    }
}

void LRCache::sample_without_update(uint64_t &t) {
    if (meta_holder[0].empty() || meta_holder[1].empty())
        return;
    /*
     * sample the cache to get in-cache distribution
     */
#ifdef LOG_SAMPLE_RATE
    bool log_flag = ((double) rand() / (RAND_MAX)) < LOG_SAMPLE_RATE;
#endif

    //sample list 0
    {
        uint32_t rand_idx = _distribution(_generator) % meta_holder[0].size();
        uint n_sample = min(sample_rate, (uint) meta_holder[0].size());

        for (uint32_t i = 0; i < n_sample; i++) {
            uint32_t pos = (i + rand_idx) % meta_holder[0].size();
            auto &meta = meta_holder[0][pos];

            //fill in past_interval
            uint8_t j = 0;
            auto past_intervals = vector<double >(n_past_intervals);
            for (j = 0; j < meta._past_timestamp_idx && j < n_past_intervals; ++j) {
                uint8_t past_timestamp_idx = (meta._past_timestamp_idx - 1 - j) % n_past_intervals;
                uint64_t past_interval = t - meta._past_timestamps[past_timestamp_idx];
                if (past_interval >= threshold)
                    past_intervals[j] = log1p_threshold;
                else
                    past_intervals[j] = log1p(past_interval);
            }
            for (; j < n_past_intervals; j++)
                past_intervals[j] = log1p_threshold;

            uint64_t known_future_interval;
            double log1p_known_future_interval;
            if (meta._future_timestamp - t < threshold) {
                known_future_interval = meta._future_timestamp - t;
                log1p_known_future_interval = log1p(known_future_interval);
            }
            else {
//                known_future_interval = threshold - 1;
                log1p_known_future_interval = log1p_threshold;
            }

#ifdef LOG_SAMPLE_RATE
            //print distribution
            if (log_flag) {
                cout << 2 <<" ";
                for (uint k = 0; k < n_past_intervals; ++k)
                    cout << past_intervals[k] << " ";
                cout << log1p_known_future_interval << endl;
            }
#endif
        }
    }
}

bool LRCache::lookup(SimpleRequest &_req) {
    auto & req = dynamic_cast<AnnotatedRequest &>(_req);
    static uint64_t i = 0;
    if (!(i%1000000)) {
        cerr << "mean diff: " << mean_diff << endl;
        for (int j = 0; j < n_past_intervals; ++j)
            cerr << "weight " << j << ": " << weights[j] << endl;
        cerr << "bias: " << bias << endl;
    }
    ++i;

    //todo: deal with size consistently
    try_train(req._t);
    if (!(i % training_sample_interval))
        sample(req._t);

    auto it = key_map.find(req._id);
    if (it != key_map.end()) {
        //update past timestamps
        bool & list_idx = it->second.first;
        uint32_t & pos_idx = it->second.second;
        meta_holder[list_idx][pos_idx].update(req._t, req._next_seq);
        return !list_idx;
    }
    return false;
}

bool LRCache::lookup_without_update(SimpleRequest &_req) {
    auto & req = dynamic_cast<AnnotatedRequest &>(_req);
    static uint64_t i = 0;
    if (!(i%1000000)) {
        cerr << "mean diff: " << mean_diff << endl;
        for (int j = 0; j < n_past_intervals; ++j)
            cerr << "weight " << j << ": " << weights[j] << endl;
        cerr << "bias: " << bias << endl;
    }
    ++i;

    //todo: deal with size consistently
    try_train(req._t);
    sample_without_update(req._t);

    auto it = key_map.find(req._id);
    if (it != key_map.end()) {
        //update past timestamps
        bool & list_idx = it->second.first;
        uint32_t & pos_idx = it->second.second;
        meta_holder[list_idx][pos_idx].update(req._t, req._next_seq);
        return !list_idx;
    }
    return false;
}

void LRCache::admit(SimpleRequest &_req) {
    AnnotatedRequest & req = static_cast<AnnotatedRequest &>(_req);
    const uint64_t & size = req._size;
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req.get_id(), size);
        return;
    }

    auto it = key_map.find(req._id);
    if (it == key_map.end()) {
        //fresh insert
        key_map.insert({req._id, {0, (uint32_t) meta_holder[0].size()}});
        meta_holder[0].emplace_back(req._id, req._size, req._t, req._next_seq);
        _currentSize += size;
        if (_currentSize <= _cacheSize)
            return;
    } else if (size + _currentSize <= _cacheSize){
        //bring list 1 to list 0
        //first move meta data, then modify hash table
        uint32_t tail0_pos = meta_holder[0].size();
        meta_holder[0].emplace_back(meta_holder[1][it->second.second]);
        uint32_t tail1_pos = meta_holder[1].size()-1;
        if (it->second.second !=  tail1_pos) {
            //swap tail
            meta_holder[1][it->second.second] = meta_holder[1][tail1_pos];
            key_map.find(meta_holder[1][tail1_pos]._key)->second.second = it->second.second;
        }
        meta_holder[1].pop_back();
        it->second = {0, tail0_pos};
        _currentSize += size;
        return;
    } else {
        //insert-evict
        auto epair = rank(req._t);
        auto & key0 = epair.first;
        auto & pos0 = epair.second;
        auto & pos1 = it->second.second;
        _currentSize = _currentSize - meta_holder[0][pos0]._size + req._size;
        swap(meta_holder[0][pos0], meta_holder[1][pos1]);
        swap(it->second, key_map.find(key0)->second);
    }
    // check more eviction needed?
    while (_currentSize > _cacheSize) {
        evict(req._t);
    }
}


pair<uint64_t, uint32_t> LRCache::rank(const uint64_t & t) {
    double max_future_interval;
    uint64_t max_key;
    uint32_t max_pos;
    uint64_t min_past_timestamp;

    uint32_t rand_idx = _distribution(_generator) % meta_holder[0].size();
    uint n_sample;
    if (sample_rate < meta_holder[0].size())
        n_sample = sample_rate;
    else
        n_sample = meta_holder[0].size();

    for (uint32_t i = 0; i < n_sample; i++) {
        uint32_t pos = (i+rand_idx)%meta_holder[0].size();
        auto & meta = meta_holder[0][pos];
        //fill in past_interval
        uint8_t j = 0;
        auto past_intervals = vector<double >(n_past_intervals);
        for (j = 0; j < meta._past_timestamp_idx && j < n_past_intervals; ++j) {
            uint8_t past_timestamp_idx = (meta._past_timestamp_idx - 1 - j) % n_past_intervals;
            uint64_t past_interval = t - meta._past_timestamps[past_timestamp_idx];
            if (past_interval >= threshold)
                past_intervals[j] = log1p_threshold;
            else
                past_intervals[j] = log1p(past_interval);
        }
        for (; j < n_past_intervals; j++)
            past_intervals[j] = log1p_threshold;

        double future_interval = 0;
        for (j = 0; j < n_past_intervals; j++)
            future_interval += weights[j] * past_intervals[j];

        uint8_t oldest_idx = (meta._past_timestamp_idx - 1)%n_past_intervals;
        uint64_t past_timestamp = meta._past_timestamps[oldest_idx];


        if (!i || future_interval > max_future_interval ||
                (future_interval == max_future_interval && (past_timestamp < min_past_timestamp))) {
            max_future_interval = future_interval;
            max_key = meta._key;
            max_pos = pos;
            min_past_timestamp = past_timestamp;
        }

        //statistics
//        double diff = future_interval+bias - log1p(meta._future_timestamp-t);
//        mean_diff = 0.99*mean_diff + 0.01*abs(diff);
//        cerr<<mean_diff<<endl;
//        if (!(tmp_i%100000)) {
//            cerr<<past_timestamps.size()<<endl;
//            ++tmp_i;
//
//        }
//
    }

    return {max_key, max_pos};
}

void LRCache::evict(const uint64_t & t) {
    auto epair = rank(t);
    uint64_t & key = epair.first;
    uint32_t & old_pos = epair.second;

    //bring list 0 to list 1
    uint32_t new_pos = meta_holder[1].size();

    meta_holder[1].emplace_back(meta_holder[0][old_pos]);
    uint32_t activate_tail_idx = meta_holder[0].size()-1;
    if (old_pos !=  activate_tail_idx) {
        //move tail
        meta_holder[0][old_pos] = meta_holder[0][activate_tail_idx];
        key_map.find(meta_holder[0][activate_tail_idx]._key)->second.second = old_pos;
    }
    meta_holder[0].pop_back();

    auto it = key_map.find(key);
    it->second.first = 1;
    it->second.second = new_pos;
    _currentSize -= meta_holder[1][new_pos]._size;
}

