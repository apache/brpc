// Copyright (c) 2018 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <thread>
#include <gtest/gtest.h>

#include "butil/containers/sync_list.h"
#include "butil/containers/sync_map.h"
#include "butil/containers/sync_ptr.h"
#include "butil/fast_rand.h"
#include "butil/logging.h"

namespace butil {

TEST(SyncMapTests, Simple) {
    sync_map<int, int> sync_map;
    int result;
    EXPECT_FALSE(sync_map.seek(2, &result));
    sync_map.insert(2, 2);
    EXPECT_TRUE(sync_map.seek(2, &result));
    EXPECT_TRUE(sync_map.seek(2, &result));
    sync_map.insert(1, 1);
    sync_map.erase(2);
    EXPECT_FALSE(sync_map.seek(2, &result));

    int cnt = 0;
    auto l = [&cnt](const int& key, const int& value) {
        std::cout << "key: " << key << ", value: " << value << std::endl;
        ++cnt;
        return true;
    };
    sync_map.range(l);
    EXPECT_EQ(cnt, 1);
}

TEST(SyncMapTests, Simple2) {
    // ========== struct ==============
    class EntryTest {
    public:
        int num;
        EntryTest() : num(0) {
            DVLOG(5) << "EntryTest build.";
        }
        explicit EntryTest(int num) : num(num) {
            DVLOG(5) << "EntryTest build.";
        }
        ~EntryTest() {
            DVLOG(5) << "EntryTest destory.";
        }
        EntryTest(const EntryTest& other) {
            DVLOG(5) << "EntryTest build from copy.";
            num = other.num;
        }
    };

    sync_map<int, EntryTest> m;
    // update one items multiple times/concurrently
    EntryTest t;

    // [[read]]  empty, false
    // [[dirty]] empty
    //
    // [[read]] empty, true
    // [[dirty]] 1, 2, 3, 4, 5
    for (int i = 0; i < 5; ++i) {
        DVLOG(5) << "[Simple2] insert " << i;
        t.num = i;
        m.insert(i, t);
        DVLOG(5) << "[Simple2] insert " << i;
        m.insert(i, t);
    }

    // [[read]] 1, 2, 3, 4, 5, false
    // [[dirty]] empty
    EXPECT_TRUE(m._read.load()->m->size() == 0);
    EXPECT_TRUE(m._read.load()->amended == true);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(m.seek(i, &t));
        DVLOG(5) << "[Simple2] seek " << i << ", " << t.num;
        EXPECT_EQ(t.num, i);
    }
    EXPECT_TRUE(m._read.load()->m->size() == 5);
    EXPECT_TRUE(m._read.load()->amended == false);
    EXPECT_TRUE(m._dirty->m->size() == 0);

    // [[read]] 2, 4, 6, 8, 10, false
    // [[dirty]] empty
    for (int i = 0; i < 5; ++i) {
        DVLOG(5) << "[Simple2] insert " << i;
        t.num = i * 2;
        m.insert(i, t);
    }
    EXPECT_TRUE(m._read.load()->m->size() == 5);
    EXPECT_TRUE(m._read.load()->amended == false);
    EXPECT_TRUE(m._dirty->m->size() == 0);

    // [[read]]  has 1, 2, 3, 4, 5, true
    // [[dirty]] has 1, 2, 3, 4, 5, 101
    DVLOG(5) << "[Simple2] insert " << 101;
    t.num = 101;
    m.insert(101, t);
    EXPECT_TRUE(m._read.load()->m->size() == 5);
    EXPECT_TRUE(m._read.load()->amended == true);
    EXPECT_EQ(m._dirty->m->size(), (size_t)6);

    // [[read]]  has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), true
    // [[dirty]] has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), 101
    for (int i = 0; i < 5; ++i) {
        DVLOG(5) << "[Simple2] erase " << i;
        m.erase(i);
    }
    EXPECT_EQ(m._read.load()->m->size(), (size_t)5);
    EXPECT_TRUE(m._read.load()->amended == true);
    EXPECT_EQ(m._dirty->m->size(), (size_t)6);

    DVLOG(5) << "[Simple2] erase " << 101;
    m.erase(101);
    // [[read]]  has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), true
    // [[dirty]] has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted)
    EXPECT_EQ(m._read.load()->m->size(), (size_t)5);
    EXPECT_TRUE(m._read.load()->amended == true);
    EXPECT_EQ(m._dirty->m->size(), (size_t)5);

    // [[read]]  has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), true
    // [[dirty]] has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), 101
    t.num = 101;
    m.insert(101, t);
    EXPECT_EQ(m._dirty->m->size(), (size_t)6);
    for (int i = 0; i < 6; ++i) {
        m.seek(101, &t);
    }

    // [[read]]  has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), 101, false
    // [[read]]  has 1(deleted), 2(deleted), 3(deleted), 4(deleted), 5(deleted), true
    EXPECT_TRUE(m._read.load()->m->size() == 6);
    EXPECT_TRUE(m._read.load()->amended == false);
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(m.seek(i, &t));
    }
    EXPECT_TRUE(m.seek(101, &t));
    EXPECT_EQ(m._dirty->m->size(), (size_t)5);
}

TEST(SyncMapTests, Simple3) {
    sync_map<int, int> sync_map;

    int write_number = 100000;
    int number_threads = 10;
    std::vector<std::thread> threads;
    for (int t = 0; t < number_threads; ++t) {
        threads.push_back(std::thread([&sync_map, t, write_number]() {
            butil::Timer timer;
            timer.start();
            for (int i = 0; i < write_number; ++i) {
                sync_map.insert(i, i);
            }
            timer.stop();
            std::cout << "[bench-3, writer-" << t << "] write " << write_number
                << " item, cost " << timer.u_elapsed() << " us, each op cost "
                << timer.u_elapsed()/write_number << " us"<< std::endl;
        }));
    }

    for (int t = 0; t < number_threads; ++t) {
        threads[t].join();
    }
}

TEST(SyncMapTests, Simple4) {
    static std::atomic<int> reclaimed(0);
    static std::atomic<int> build(0);

    class EntryTest {
    public:
        int num;
        EntryTest() : num(0) {
            DVLOG(5) << "EntryTest build.";
            ++build;
        }
        explicit EntryTest(int num) : num(num) {
            DVLOG(5) << "EntryTest build.";
            ++build;
        }
        ~EntryTest() {
            DVLOG(5) << "EntryTest destory.";
            ++reclaimed;
        }
        EntryTest(const EntryTest& other) {
            DVLOG(5) << "EntryTest build from copy.";
            num = other.num;
            ++build;
        }
    };

    sync_map<int, EntryTest> sync_map;
    int write_number = 100000;
    int number_threads = 10;
    std::vector<std::thread> threads;
    for (int t = 0; t < number_threads; ++t) {
        threads.push_back(std::thread([&sync_map, t, write_number]() {
            butil::Timer timer;
            timer.start();
            for (int i = 0; i < write_number; ++i) {
                sync_map.insert(i, EntryTest(i));
            }
            timer.stop();
            std::cout << "[bench-4, writer-" << t << "] write " << write_number
                << " item, cost " << timer.u_elapsed() << " us, each op cost "
                << timer.u_elapsed()/write_number << " us"<< std::endl;
        }));

        threads.push_back(std::thread([&sync_map, t, write_number]() {
            butil::Timer timer;
            timer.start();
            for (int i = 0; i < write_number; ++i) {
                EntryTest temp;
                sync_map.seek(i, &temp);
            }
            timer.stop();
            std::cout << "[bench-4, reader-" << t << "] find " << write_number
                << " item, cost " << timer.u_elapsed() << " us, each op cost "
                << timer.u_elapsed()/write_number << " us"<< std::endl;
        }));
    }

    for (auto& t : threads) { t.join(); }
    //sync_map.check_to_reclaim(true);
    std::cout << "build " << build << " times, "
        << "reclaimed " << reclaimed << " times" << std::endl;

    // Since sync_map is not destructed, 10W EntryTest is stored in sync_map
    SyncPtrCtrl::GetInstance()->CleanUp();
    EXPECT_EQ(build - 100000, reclaimed);
}

TEST(SyncMapTests, Bench) {
    sync_map<int, int> sync_map;
    int write_number = 100;
    int read_number = 200;
    int erase_number = 10;
    std::thread writer1([&sync_map, write_number]() {
        butil::Timer timer;
        timer.start();
        for (int i = 0; i < write_number; ++i) {
            sync_map.insert(i, i);
        }
        timer.stop();
        std::cout << "[writer1] write " << write_number << " item, cost "
            << timer.u_elapsed() << " us" << std::endl;
    });
    writer1.join();

    std::thread reader1([&sync_map, read_number]() {
        butil::Timer timer;
        int result = 0;
        timer.start();
        for (int i = 0; i < read_number; ++i) {
            result = 0;
            sync_map.seek(i, &result);
            DVLOG(5) << "[reader1] read key " << i << ", value " << result;
        }
        timer.stop();
        std::cout << "[reader1] find " << read_number << " item, cost "
            << timer.u_elapsed() << " us" << std::endl;
    });
    std::thread reader2([&sync_map, read_number]() {
        butil::Timer timer;
        int result = 0;
        timer.start();
        for (int i = 0; i < read_number; ++i) {
            result = 0;
            sync_map.seek(i, &result);
            DVLOG(5) << "[reader2] read key " << i << ", value " << result;
        }
        timer.stop();
        std::cout << "[reader2] find " << read_number << " item, cost "
            << timer.u_elapsed() << " us" << std::endl;
    });
    reader1.join();
    reader2.join();

    std::thread eraser([&sync_map, erase_number]() {
        butil::Timer timer;
        timer.start();
        for (int i = 0; i < erase_number; ++i) {
            sync_map.erase(i);
        }
        timer.stop();
        std::cout << "[reader2] erase " << erase_number << " item, cost "
            << timer.u_elapsed() << " us" << std::endl;
    });
    eraser.join();
}

TEST(SyncMapTests, Bench2) {
    sync_map<int, int> sync_map;
    int write_number = 10000;
    int read_number = 100000;

    std::thread writer1([&sync_map, write_number]() {
        butil::Timer timer;
        timer.start();
        for (int i = 0; i < write_number; ++i) {
            sync_map.insert(i, i);
        }
        timer.stop();
        std::cout << "[bench-2, writer] write " << write_number << " item, cost "
            << timer.u_elapsed() << " us, each op cost "
            << timer.u_elapsed() / write_number << " us" << std::endl;
    });

    // more threads to concurrent read
    int number_threads = 10;
    std::vector<std::thread> threads;
    for (int t = 0; t < number_threads; ++t) {
        threads.push_back(std::thread([&sync_map, t, read_number]() {
            butil::Timer timer;
            int result = 0;
            timer.start();
            for (int i = 0; i < read_number; ++i) {
                sync_map.seek(i, &result);
            }
            timer.stop();
            std::cout << "[bench-2, reader-" << t << "] find " << read_number
                << " item, cost " << timer.u_elapsed() << " us, each op cost "
                << timer.u_elapsed()/read_number << " us"<< std::endl;
        }));
    }

    writer1.join();
    for (int t = 0; t < number_threads; ++t) {
        threads[t].join();
    }
}

class MyLinkNode : public LinkNode {
public:
    MyLinkNode(int number) : _number(number) {}
    ~MyLinkNode() {
        VLOG(4) << "destory: " << _number;
    }
    int number() { return _number; }
private:
    int _number{0};
};

TEST(SyncListTests, LinkList) {
    LinkedList list;
    for (int i = 0; i < 10; ++i) {
        list.push(new MyLinkNode(i));
    }
    auto p = list.head();
    while (p) {
        auto temp = p;
        p = p->next();
        delete temp;
    }
}

TEST(SyncListTests, sync_list) {
    sync_list list;

    std::thread writer1(
        [&list]() {
        for (int i = 0; i < 10; ++i) {
            list.push(new MyLinkNode(i));
        }
    });
    std::thread writer2(
        [&list]() {
        for (int i = 10; i < 20; ++i) {
            list.push(new MyLinkNode(i));
        }
    });

    std::thread reader1(
        [&list]() {
        LinkedList all = list.pop_all();
        auto p = all.head();
        while (p) {
            auto temp = p;
            p = p->next();
            delete temp;
        }
    });

    writer1.join();
    writer2.join();
    reader1.join();

    LinkedList all = list.pop_all();
    auto p = all.head();
    while (p) {
        auto temp = p;
        p = p->next();
        delete temp;
    }
}

static std::atomic<unsigned> s_construct_count(0);
static std::atomic<unsigned> s_destruct_count(0);
class MySyncObject : public SyncObject<MySyncObject> {
public:
    MySyncObject(int tag) : _tag(tag) {
        ++s_construct_count;
        DVLOG(5) << "MySyncObject() tag=" << _tag;
    }
    ~MySyncObject() override {
        ++s_destruct_count;
        DVLOG(5) << "~MySyncObject() tag=" << _tag;
    }

    int tag() const { return _tag; }
private:
    int _tag;
};

TEST(SyncPointerTests, sync_ptr) {
    std::atomic<MySyncObject*> msp(new MySyncObject(1));

    unsigned reader_number = 10;
    unsigned updater_number = 10;

    unsigned range = 10000;
    unsigned update_times = 100;
    unsigned random = butil::fast_rand_in((unsigned)0,
                        (unsigned)(range - update_times));

    std::vector<std::thread> readers;
    for (unsigned i = 0; i < reader_number; ++i) {
        readers.push_back(std::thread([=, &msp]() {

            for (unsigned i = 0; i < range; ++i) {
                sync_ptr<MySyncObject> ptr(msp);
                EXPECT_LE(ptr->tag(), random + update_times);
            }
        }));
    }

    std::vector<std::thread> updaters;
    for (unsigned i = 0; i < updater_number; ++i) {
        updaters.push_back(std::thread([=, &msp]() {
            for (unsigned j = 0; j < range; ++j) {
                if (j >= random && j < random + update_times) {
                    auto* n = new MySyncObject(j);
                    auto* p = msp.load(std::memory_order_relaxed);
                    while (true) {
                        if (msp.compare_exchange_weak(p, n)) {
                            break;
                        }
                    }
                    EXPECT_GE(p->tag(), 0);
                    p->Retire();
                }
            }
        }));
    }

    std::for_each(readers.begin(), readers.end(),
        [](std::thread& thr) { thr.join(); });
    std::for_each(updaters.begin(), updaters.end(),
        [](std::thread& thr) { thr.join(); });
    delete msp.load();
    SyncPtrCtrl::GetInstance()->CleanUp();
    EXPECT_EQ(s_construct_count.load(), s_destruct_count.load());
}

} // namespace butil
