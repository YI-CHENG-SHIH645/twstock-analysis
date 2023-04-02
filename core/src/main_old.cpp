#include <iostream>
#include <map>
#include <omp.h>
#include <pthread.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#define THREAD_NUM 16

pthread_mutex_t mutex;
pthread_mutex_t mutex2;

void *thread_func(void *param);
void *thread_func2(void *param);
namespace py = pybind11;

bool sell_logic(int hd, int hd_th, float adj_c, float adj_c_ma20) {
  return (hd >= hd_th) || (adj_c < adj_c_ma20);
}

std::tuple<float, float, float> get_pnl(float open_price, float sell_price) {
  float tax = 0.003f * sell_price;
  float fee = 0.001425f * 0.6f * (sell_price + open_price);
  float pnl = (sell_price - open_price - tax - fee) / open_price;
  tax = std::round(tax * 1000 * 100) / 100;
  fee = std::round(fee * 1000 * 100) / 100;
  pnl = std::round(pnl * 100 * 100) / 100;

  return std::make_tuple(pnl, tax, fee);
}

void trade(std::map<int, std::map<std::string, std::string>> &dic_records,
           int tid, std::string &sid, float open_price, int holding_days,
           int holding_days_th,
           std::map<std::string, std::vector<std::string>> &last_date_signal,
           int &available_tid, std::vector<float> &o, std::vector<float> &c,
           std::vector<float> &ma20, std::vector<std::string> &dates,
           std::map<std::string, std::set<std::string>> &selected,
           std::string &strategy_name, std::string &trader_code) {

  auto &last_date_sell_list = last_date_signal["sell"];

  for (size_t idx = 0; idx < o.size(); ++idx) {
    if (!std::isnan(open_price)) {
      holding_days += (idx != o.size() - 1);
      // try to sell
      if (sell_logic(holding_days, holding_days_th, c[idx], ma20[idx])) {
        if (idx == o.size() - 1) {
          last_date_sell_list.push_back(sid);
          break;
        }

        auto sell_price = o[idx + 1];
        auto triplet = get_pnl(open_price, sell_price);
        auto sell_date = dates[idx + 1];

        dic_records[tid]["close_date"] = sell_date;
        dic_records[tid]["close_price"] = std::to_string(sell_price);
        dic_records[tid]["holding_days"] = std::to_string(holding_days);
        dic_records[tid]["pnl"] = std::to_string(std::get<0>(triplet));
        dic_records[tid]["tax"] = std::to_string(std::get<1>(triplet));
        dic_records[tid]["fee"] = std::to_string(std::get<2>(triplet));

        open_price = std::stof("NAN");
        holding_days = 0;

        tid = available_tid;
        available_tid += 1;
        dic_records[tid] = {{"sid", sid},
                            {"strategy_name", strategy_name},
                            {"trader_code", trader_code},
                            {"holding_days", "0"},
                            {"last_check", "today"},
                            {"open_price", "NAN"}};
      }
    } else {
      // try to buy
      if (selected[dates[idx]].count(sid)) {
        if (idx < o.size() - 1) {
          auto buy_date = dates[idx + 1];
          float buy_price = o[idx + 1];
          dic_records[tid]["open_date"] = buy_date;
          dic_records[tid]["open_price"] = std::to_string(buy_price);
          dic_records[tid]["long_short"] = "long";
          dic_records[tid]["shares"] = "1";
          open_price = buy_price;
          holding_days = 1;
        }
      }
    }
  }
  if (!std::isnan(open_price)) {
    auto triplet = get_pnl(open_price, c[c.size() - 1]);
    dic_records[tid]["holding_days"] = std::to_string(holding_days);
    dic_records[tid]["pnl"] = std::to_string(std::get<0>(triplet));
  }
}

void trade_pthread(
    std::map<int, std::map<std::string, std::string>> &dic_records, int tid,
    std::string &sid, float open_price, int holding_days, int holding_days_th,
    std::map<std::string, std::vector<std::string>> &last_date_signal,
    int &available_tid, std::vector<float> &o, std::vector<float> &c,
    std::vector<float> &ma20, std::vector<std::string> &dates,
    std::map<std::string, std::set<std::string>> &selected,
    std::string &strategy_name, std::string &trader_code) {

  auto &last_date_sell_list = last_date_signal["sell"];

  for (size_t idx = 0; idx < o.size(); ++idx) {
    if (!std::isnan(open_price)) {
      holding_days += (idx != o.size() - 1);
      // try to sell
      if (sell_logic(holding_days, holding_days_th, c[idx], ma20[idx])) {
        if (idx == o.size() - 1) {
          pthread_mutex_lock(&mutex2); // lock
          last_date_sell_list.push_back(sid);
          pthread_mutex_unlock(&mutex2); // Unlock
          break;
        }

        auto sell_price = o[idx + 1];
        auto triplet = get_pnl(open_price, sell_price);
        auto sell_date = dates[idx + 1];

        // 為什麼這裡 assert 不會過？ 一定要 if
        if (auto dr = dic_records.find(tid); dr != dic_records.end()) {
          //        assert(dr != dic_records.end());
          dr->second.find("close_date")->second = sell_date;
          dr->second.find("close_price")->second = std::to_string(sell_price);
          dr->second.find("holding_days")->second =
              std::to_string(holding_days);
          dr->second.find("pnl")->second = std::to_string(std::get<0>(triplet));
          dr->second.find("tax")->second = std::to_string(std::get<1>(triplet));
          dr->second.find("fee")->second = std::to_string(std::get<2>(triplet));
        }

        open_price = std::stof("NAN");
        holding_days = 0;

        std::map<std::string, std::string> r = {
            {"sid", sid},
            {"strategy_name", strategy_name},
            {"trader_code", trader_code},
            {"holding_days", "0"},
            {"last_check", "today"},
            {"open_price", "NAN"},
            {"open_date", "NAN"},
            {"close_price", "NAN"},
            {"close_date", "NAN"},
            {"holding_days", "NAN"},
            {"pnl", "NAN"},
            {"tax", "NAN"},
            {"fee", "NAN"},
            {"long_short", "NAN"},
            {"shares", "NAN"}};
        tid = __sync_fetch_and_add(&available_tid, 1);
        pthread_mutex_lock(&mutex); // lock
        dic_records.insert({tid, r});
        pthread_mutex_unlock(&mutex); // Unlock
      }
    } else {
      // try to buy
      if (auto s = selected.find(dates[idx]);
          s != selected.end() && s->second.count(sid)) {
        if (idx < o.size() - 1) {
          auto buy_date = dates[idx + 1];
          float buy_price = o[idx + 1];

          // 為什麼這裡 assert 不會過？ 一定要 if
          if (auto dr = dic_records.find(tid); dr != dic_records.end()) {
            //          assert(dr != dic_records.end());
            dr->second.find("open_date")->second = buy_date;
            dr->second.find("open_price")->second = std::to_string(buy_price);
            dr->second.find("long_short")->second = "long";
            dr->second.find("shares")->second = "1";
          }

          open_price = buy_price;
          holding_days = 1;
        }
      }
    }
  }
  if (!std::isnan(open_price)) {
    auto triplet = get_pnl(open_price, c[c.size() - 1]);

    // 為什麼這裡 assert 不會過？ 一定要 if
    if (auto dr = dic_records.find(tid); dr != dic_records.end()) {
      //    assert(dr != dic_records.end());
      dr->second.find("holding_days")->second = std::to_string(holding_days);
      dr->second.find("pnl")->second = std::to_string(std::get<0>(triplet));
    }
  }
}

void trade_omp(
    std::map<int, std::map<std::string, std::string>> &dic_records, int tid,
    std::string &sid, float open_price, int holding_days, int holding_days_th,
    std::map<std::string, std::vector<std::string>> &last_date_signal,
    int &available_tid, std::vector<float> &o, std::vector<float> &c,
    std::vector<float> &ma20, std::vector<std::string> &dates,
    std::map<std::string, std::set<std::string>> &selected,
    std::string &strategy_name, std::string &trader_code) {

  auto &last_date_sell_list = last_date_signal["sell"];

  for (size_t idx = 0; idx < o.size(); ++idx) {
    if (!std::isnan(open_price)) {
      holding_days += (idx != o.size() - 1);
      // try to sell
      if (sell_logic(holding_days, holding_days_th, c[idx], ma20[idx])) {
        if (idx == o.size() - 1) {
#pragma omp critical
          { last_date_sell_list.push_back(sid); }
          break;
        }

        auto sell_price = o[idx + 1];
        auto triplet = get_pnl(open_price, sell_price);
        auto sell_date = dates[idx + 1];

        // 為什麼這裡 assert 不會過？ 一定要 if
        if (auto dr = dic_records.find(tid); dr != dic_records.end()) {
          assert(dr != dic_records.end());
          dr->second.find("close_date")->second = sell_date;
          dr->second.find("close_price")->second = std::to_string(sell_price);
          dr->second.find("holding_days")->second =
              std::to_string(holding_days);
          dr->second.find("pnl")->second = std::to_string(std::get<0>(triplet));
          dr->second.find("tax")->second = std::to_string(std::get<1>(triplet));
          dr->second.find("fee")->second = std::to_string(std::get<2>(triplet));
        } else {
          for (int i = 0; i < 100; ++i) {
            std::cout << "sell thread : " << omp_get_thread_num()
                      << ", deal with " << sid << ", finding tid fail: " << tid
                      << ", available: " << available_tid << std::endl;
          }
        }

        open_price = std::stof("NAN");
        holding_days = 0;

        std::map<std::string, std::string> r = {
            {"sid", sid},
            {"strategy_name", strategy_name},
            {"trader_code", trader_code},
            {"holding_days", "0"},
            {"last_check", "today"},
            {"open_price", "NAN"},
            {"open_date", "NAN"},
            {"close_price", "NAN"},
            {"close_date", "NAN"},
            {"holding_days", "NAN"},
            {"pnl", "NAN"},
            {"tax", "NAN"},
            {"fee", "NAN"},
            {"long_short", "NAN"},
            {"shares", "NAN"}};

        tid = __sync_fetch_and_add(&available_tid, 1);
#pragma omp critical
        { dic_records.insert({tid, r}); }
      }
    } else {
      // try to buy
      if (auto s = selected.find(dates[idx]);
          s != selected.end() && s->second.count(sid)) {
        if (idx < o.size() - 1) {
          auto buy_date = dates[idx + 1];
          float buy_price = o[idx + 1];
          assert(!std::isnan(buy_price));

          // 為什麼這裡 assert 不會過？ 一定要 if
          if (auto dr = dic_records.find(tid); dr != dic_records.end()) {
            assert(dr != dic_records.end());
            dr->second.find("open_date")->second = buy_date;
            dr->second.find("open_price")->second = std::to_string(buy_price);
            dr->second.find("long_short")->second = "long";
            dr->second.find("shares")->second = "1";
          } else {
            for (int i = 0; i < 100; ++i) {
              std::cout << "buy thread : " << omp_get_thread_num()
                        << ", deal with " << sid
                        << ", finding tid fail: " << tid
                        << ", available: " << available_tid << std::endl;
            }
          }

          open_price = buy_price;
          holding_days = 1;
        }
      }
    }
  }
  if (!std::isnan(open_price)) {
    auto triplet = get_pnl(open_price, c[c.size() - 1]);

    // 為什麼這裡 assert 不會過？ 一定要 if
    if (auto dr = dic_records.find(tid); dr != dic_records.end()) {
      assert(dr != dic_records.end());
      dr->second.find("holding_days")->second = std::to_string(holding_days);
      dr->second.find("pnl")->second = std::to_string(std::get<0>(triplet));
    } else {
      for (int i = 0; i < 100; ++i) {
        std::cout << "end thread : " << omp_get_thread_num() << ", deal with "
                  << sid << ", finding tid fail: " << tid
                  << ", available: " << available_tid << std::endl;
      }
    }
  }
}

// TODO: parallelize this
std::map<int, std::map<std::string, std::string>> trade_on_sids(
    std::vector<std::string> sids, std::map<std::string, std::vector<float>> o,
    std::map<std::string, std::vector<float>> c,
    std::map<std::string, std::vector<float>> ma20,
    std::vector<std::string> dates,
    std::map<std::string, std::set<std::string>> selected, int holding_days_th,
    std::map<int, std::map<std::string, std::string>> dic_records,
    std::map<std::string, std::vector<std::string>> last_date_signal,
    std::map<std::string, int> sid2tid, int available_tid,
    std::string strategy_name, std::string trader_code) {
  for (auto it = sids.begin(); it < sids.end(); ++it) {
    int tid;
    std::map<std::string, std::string> r;
    if (!sid2tid.count(*it)) {
      r = {{"sid", *it},
           {"strategy_name", strategy_name},
           {"trader_code", trader_code},
           {"holding_days", "0"},
           {"last_check", "today"},
           {"open_price", "NAN"}};
      tid = available_tid;
      available_tid += 1;
      dic_records[tid] = r;
    } else {
      tid = sid2tid[*it];
      dic_records[tid]["last_check"] = "today";
      r = dic_records[sid2tid[*it]];
    }

    float open_price = std::stof(r["open_price"]);
    int holding_days = std::stoi(r["holding_days"]);
    trade(dic_records, tid, *it, open_price, holding_days, holding_days_th,
          last_date_signal, available_tid, o[*it], c[*it], ma20[*it], dates,
          selected, strategy_name, trader_code);
  }

  return dic_records;
}

typedef struct thread_arg {
  int id;
  std::vector<std::string> *sids;
  std::map<std::string, std::vector<float>> *o;
  std::map<std::string, std::vector<float>> *c;
  std::map<std::string, std::vector<float>> *ma20;
  std::vector<std::string> *dates;
  std::map<std::string, std::set<std::string>> *selected;
  int *holding_days_th;
  std::map<int, std::map<std::string, std::string>> *dic_records;
  std::map<std::string, std::vector<std::string>> *last_date_signal;
  std::map<std::string, int> *sid2tid;
  int *available_tid;
  std::string *strategy_name;
  std::string *trader_code;
} thread_arg;

std::map<int, std::map<std::string, std::string>> trade_on_sids_pthread(
    std::vector<std::string> sids, std::map<std::string, std::vector<float>> o,
    std::map<std::string, std::vector<float>> c,
    std::map<std::string, std::vector<float>> ma20,
    std::vector<std::string> dates,
    std::map<std::string, std::set<std::string>> selected, int holding_days_th,
    std::map<int, std::map<std::string, std::string>> dic_records,
    std::map<std::string, std::vector<std::string>> last_date_signal,
    std::map<std::string, int> sid2tid, int available_tid,
    std::string strategy_name, std::string trader_code) {

  //  Create the thread
  pthread_t *threads;
  threads = (pthread_t *)malloc(THREAD_NUM * sizeof(pthread_t));

  //  InitMutex
  pthread_mutex_init(&mutex, nullptr);
  pthread_mutex_init(&mutex2, nullptr);

  //  Create thread arguments
  thread_arg th_arg[THREAD_NUM];

  //  Create threads
  for (int i = 0; i < THREAD_NUM; i++) {
    th_arg[i].id = i;
    th_arg[i].sids = &sids; //****
    th_arg[i].o = &o;
    th_arg[i].c = &c;
    th_arg[i].ma20 = &ma20;
    th_arg[i].dates = &dates;
    th_arg[i].selected = &selected;
    th_arg[i].holding_days_th = &holding_days_th;
    th_arg[i].dic_records = &dic_records;
    th_arg[i].last_date_signal = &last_date_signal;
    th_arg[i].sid2tid = &sid2tid;
    th_arg[i].available_tid = &available_tid;
    th_arg[i].strategy_name = &strategy_name;
    th_arg[i].trader_code = &trader_code;
    pthread_create(&(threads[i]), nullptr, thread_func, (void *)&th_arg[i]);
  }

  //  wait for threads to complete their missions
  for (int i = 0; i < THREAD_NUM; i++)
    pthread_join(threads[i], nullptr);

  //  destroy mutex
  pthread_mutex_destroy(&mutex);
  pthread_mutex_destroy(&mutex2);

  free(threads);

  return dic_records;
}

std::map<int, std::map<std::string, std::string>> trade_on_sids_pthread2(
    std::vector<std::string> sids, std::map<std::string, std::vector<float>> o,
    std::map<std::string, std::vector<float>> c,
    std::map<std::string, std::vector<float>> ma20,
    std::vector<std::string> dates,
    std::map<std::string, std::set<std::string>> selected, int holding_days_th,
    std::map<int, std::map<std::string, std::string>> dic_records,
    std::map<std::string, std::vector<std::string>> last_date_signal,
    std::map<std::string, int> sid2tid, int available_tid,
    std::string strategy_name, std::string trader_code) {

  //  Create the thread
  pthread_t *threads;
  threads = (pthread_t *)malloc(THREAD_NUM * sizeof(pthread_t));

  //  InitMutex
  pthread_mutex_init(&mutex, nullptr);
  pthread_mutex_init(&mutex2, nullptr);

  //  Create thread arguments
  thread_arg th_arg[THREAD_NUM];

  //  Create threads
  for (int i = 0; i < THREAD_NUM; i++) {
    th_arg[i].id = i;
    th_arg[i].sids = &sids; //****
    th_arg[i].o = &o;
    th_arg[i].c = &c;
    th_arg[i].ma20 = &ma20;
    th_arg[i].dates = &dates;
    th_arg[i].selected = &selected;
    th_arg[i].holding_days_th = &holding_days_th;
    th_arg[i].dic_records = &dic_records;
    th_arg[i].last_date_signal = &last_date_signal;
    th_arg[i].sid2tid = &sid2tid;
    th_arg[i].available_tid = &available_tid;
    th_arg[i].strategy_name = &strategy_name;
    th_arg[i].trader_code = &trader_code;
    pthread_create(&(threads[i]), nullptr, thread_func2, (void *)&th_arg[i]);
  }

  //  wait for threads to complete their missions
  for (int i = 0; i < THREAD_NUM; i++)
    pthread_join(threads[i], nullptr);

  //  destroy mutex
  pthread_mutex_destroy(&mutex);
  pthread_mutex_destroy(&mutex2);

  free(threads);

  return dic_records;
}

void *thread_func(void *param) {

  thread_arg t = *(thread_arg *)param;
  int step = (int)t.sids->size() / THREAD_NUM;
  auto sid = &((*t.sids)[t.id * step]);
  auto sid_end =
      sid +
      ((t.id == THREAD_NUM - 1) ? (step + t.sids->size() % THREAD_NUM) : step);
  for (auto it = sid; it < sid_end; ++it) {

    //    omp_get_thread_num();
    int tid;
    std::map<std::string, std::string> r;

    if (auto search = t.sid2tid->find(*it); search == t.sid2tid->end()) {
      r = {{"sid", *it},
           {"strategy_name", *(t.strategy_name)},
           {"trader_code", *(t.trader_code)},
           {"holding_days", "0"},
           {"last_check", "today"},
           {"open_price", "NAN"},
           {"open_date", "NAN"},
           {"close_price", "NAN"},
           {"close_date", "NAN"},
           {"holding_days", "NAN"},
           {"pnl", "NAN"},
           {"tax", "NAN"},
           {"fee", "NAN"},
           {"long_short", "NAN"},
           {"shares", "NAN"}};

      tid = __sync_fetch_and_add(t.available_tid, 1);
      pthread_mutex_lock(&mutex); // lock
      t.dic_records->insert({tid, r});
      pthread_mutex_unlock(&mutex); // Unlock
    } else {
      tid = t.sid2tid->find(*it)->second;
      t.dic_records->find(tid)->second.find("last_check")->second = "today";
      r = t.dic_records->find(t.sid2tid->find(*it)->second)->second;
    }

    float open_price = std::stof(r.find("open_price")->second);
    int holding_days = std::stoi(r.find("holding_days")->second);

    // 為什麼這裡 assert 不會過？
    assert(t.dic_records->find(tid) != t.dic_records->end());
    trade_pthread(*(t.dic_records), tid, *it, open_price, holding_days,
                  *(t.holding_days_th), *(t.last_date_signal),
                  *(t.available_tid), t.o->find(*it)->second,
                  t.c->find(*it)->second, t.ma20->find(*it)->second, *(t.dates),
                  *(t.selected), *(t.strategy_name), *(t.trader_code));
  }
  pthread_exit(nullptr);
}

void *thread_func2(void *param) {

  thread_arg t = *(thread_arg *)param;
  // auto sid = &((*t.sids)[t.id]);
  auto sid = t.sids->begin() + t.id;
  for (auto it = sid; it < t.sids->end(); it += THREAD_NUM) {

    //    omp_get_thread_num();
    int tid;
    std::map<std::string, std::string> r;

    if (auto search = t.sid2tid->find(*it); search == t.sid2tid->end()) {
      r = {{"sid", *it},
           {"strategy_name", *(t.strategy_name)},
           {"trader_code", *(t.trader_code)},
           {"holding_days", "0"},
           {"last_check", "today"},
           {"open_price", "NAN"},
           {"open_date", "NAN"},
           {"close_price", "NAN"},
           {"close_date", "NAN"},
           {"holding_days", "NAN"},
           {"pnl", "NAN"},
           {"tax", "NAN"},
           {"fee", "NAN"},
           {"long_short", "NAN"},
           {"shares", "NAN"}};

      tid = __sync_fetch_and_add(t.available_tid, 1);
      pthread_mutex_lock(&mutex); // lock
      t.dic_records->insert({tid, r});
      pthread_mutex_unlock(&mutex); // Unlock
    } else {
      tid = t.sid2tid->find(*it)->second;
      t.dic_records->find(tid)->second.find("last_check")->second = "today";
      r = t.dic_records->find(t.sid2tid->find(*it)->second)->second;
    }

    float open_price = std::stof(r.find("open_price")->second);
    int holding_days = std::stoi(r.find("holding_days")->second);

    // 為什麼這裡 assert 不會過？
    assert(t.dic_records->find(tid) != t.dic_records->end());
    trade_pthread(*(t.dic_records), tid, *it, open_price, holding_days,
                  *(t.holding_days_th), *(t.last_date_signal),
                  *(t.available_tid), t.o->find(*it)->second,
                  t.c->find(*it)->second, t.ma20->find(*it)->second, *(t.dates),
                  *(t.selected), *(t.strategy_name), *(t.trader_code));
  }
  pthread_exit(nullptr);
}

std::map<int, std::map<std::string, std::string>> trade_on_sids_openmp(
    std::vector<std::string> & sids, std::map<std::string, std::vector<float>> & o,
    std::map<std::string, std::vector<float>> & c,
    std::map<std::string, std::vector<float>> & ma20,
    std::vector<std::string> & dates,
    std::map<std::string, std::set<std::string>> & selected, int holding_days_th,
    std::map<int, std::map<std::string, std::string>> dic_records,
    std::map<std::string, std::vector<std::string>> & last_date_signal,
    std::map<std::string, int> & sid2tid, int available_tid,
    std::string & strategy_name, std::string & trader_code) {

#pragma omp parallel for shared(dic_records, available_tid) firstprivate(sids,\
sid2tid, strategy_name, trader_code, o, c, ma20, dates, selected, last_date_signal, \
 holding_days_th) default(none)
  for (auto it = sids.begin(); it < sids.end(); ++it) {
    //    std::cout << "Hello from thread " << omp_get_thread_num() << " : " <<
    //    *it << std::endl; omp_get_thread_num();
    int tid;
    std::map<std::string, std::string> r;

    if (auto search = sid2tid.find(*it); search == sid2tid.end()) {
      r = {{"sid", *it},
           {"strategy_name", strategy_name},
           {"trader_code", trader_code},
           {"holding_days", "0"},
           {"last_check", "today"},
           {"open_price", "NAN"},
           {"open_date", "NAN"},
           {"close_price", "NAN"},
           {"close_date", "NAN"},
           {"holding_days", "NAN"},
           {"pnl", "NAN"},
           {"tax", "NAN"},
           {"fee", "NAN"},
           {"long_short", "NAN"},
           {"shares", "NAN"}};

      tid = __sync_fetch_and_add(&available_tid, 1);
#pragma omp critical
      { dic_records.insert({tid, r}); }
    } else {
      tid = sid2tid.find(*it)->second;
      dic_records.find(tid)->second.find("last_check")->second = "today";
      r = dic_records.find(sid2tid.find(*it)->second)->second;
    }

    float open_price = std::stof(r.find("open_price")->second);
    int holding_days = std::stoi(r.find("holding_days")->second);

    // 為什麼這裡 assert 不會過？
    if (dic_records.find(tid) == dic_records.end()) {
//      for (int i = 0; i < 100; ++i) {
//        std::cout << "Outside thread : " << omp_get_thread_num()
//                  << ", deal with " << *it << ", finding tid fail: " << tid
//                  << ", available: " << available_tid << std::endl;
//      }
    }
    trade_omp(dic_records, tid, *it, open_price, holding_days, holding_days_th,
              last_date_signal, available_tid, o.find(*it)->second,
              c.find(*it)->second, ma20.find(*it)->second, dates, selected,
              strategy_name, trader_code);
  }

  return dic_records;
}

PYBIND11_MODULE(core, m) {
  //  m.def("trade_on_sids", &trade_on_sids);
  m.def("trade_on_sids", &trade_on_sids);
  m.def("trade_on_sids_openmp", &trade_on_sids_openmp);
  m.def("trade_on_sids_pthread", &trade_on_sids_pthread);
  m.def("trade_on_sids_pthread2", &trade_on_sids_pthread2);
}
