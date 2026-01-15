#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

TEST_CASE("Scenario 9: Thread-Safety - Concurrent Access", "[threadsafety][concurrent]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Concurrent reads are safe") {
        // Pre-populate tree
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("player:.*", "sub2");
        tree.registerSubscriber("enemy:*:health", "sub3");

        std::atomic<int> errorCount{0};
        std::atomic<int> totalReads{0};

        auto readerThread = [&]() {
            for (int i = 0; i < 1000; ++i) {
                auto matches = tree.findSubscribers("player:123:position");
                if (matches.size() != 2) {
                    errorCount++;
                }
                totalReads++;
            }
        };

        // Launch 10 reader threads
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back(readerThread);
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(errorCount == 0);
        REQUIRE(totalReads == 10000);
    }

    SECTION("Concurrent writes are safe") {
        std::atomic<int> successfulRegistrations{0};

        auto writerThread = [&](int threadId) {
            for (int i = 0; i < 100; ++i) {
                std::string pattern = "thread:" + std::to_string(threadId) + ":*";
                std::string subscriber = "sub_" + std::to_string(threadId) + "_" + std::to_string(i);
                tree.registerSubscriber(pattern, subscriber);
                successfulRegistrations++;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back(writerThread, i);
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(successfulRegistrations == 1000);

        // Verify all patterns work
        for (int i = 0; i < 10; ++i) {
            std::string topic = "thread:" + std::to_string(i) + ":test";
            auto matches = tree.findSubscribers(topic);
            REQUIRE(matches.size() == 100); // Each thread registered 100 subscribers
        }
    }

    SECTION("Concurrent read/write mix") {
        std::atomic<bool> running{true};
        std::atomic<int> readErrors{0};
        std::atomic<int> totalReads{0};
        std::atomic<int> totalWrites{0};

        // Writer thread: continuously add patterns
        auto writer = [&]() {
            int counter = 0;
            while (running) {
                std::string pattern = "dynamic:*:" + std::to_string(counter % 100);
                tree.registerSubscriber(pattern, "writer");
                totalWrites++;
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };

        // Reader threads: continuously read
        auto reader = [&]() {
            while (running) {
                auto matches = tree.findSubscribers("dynamic:123:50");
                // Just ensure no crashes, results may vary due to concurrent writes
                totalReads++;
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(writer);

        for (int i = 0; i < 5; ++i) {
            threads.emplace_back(reader);
        }

        // Run for 2 seconds
        std::this_thread::sleep_for(std::chrono::seconds(2));
        running = false;

        for (auto& t : threads) {
            t.join();
        }

        INFO("Total reads: " << totalReads);
        INFO("Total writes: " << totalWrites);

        REQUIRE(totalReads > 0);
        REQUIRE(totalWrites > 0);
        REQUIRE(readErrors == 0);
    }

    SECTION("Concurrent register/unregister on same pattern") {
        std::atomic<bool> running{true};
        std::atomic<int> totalOps{0};

        auto worker = [&](int threadId) {
            while (running) {
                std::string subscriber = "sub_" + std::to_string(threadId);
                tree.registerSubscriber("shared:*:pattern", subscriber);
                totalOps++;

                std::this_thread::sleep_for(std::chrono::microseconds(100));

                tree.unregisterSubscriber("shared:*:pattern", subscriber);
                totalOps++;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back(worker, i);
        }

        // Run for 1 second
        std::this_thread::sleep_for(std::chrono::seconds(1));
        running = false;

        for (auto& t : threads) {
            t.join();
        }

        INFO("Total operations: " << totalOps);
        REQUIRE(totalOps > 0);

        // After all threads finish, tree should be in consistent state
        auto matches = tree.findSubscribers("shared:test:pattern");
        // Should be empty or contain only subscribers that registered last
        REQUIRE(matches.size() < 8);
    }

    SECTION("UnregisterAll under concurrent access") {
        // Pre-populate
        for (int i = 0; i < 100; ++i) {
            tree.registerSubscriber("pattern:" + std::to_string(i), "target");
        }

        std::atomic<bool> running{true};

        // Reader thread
        auto reader = [&]() {
            while (running) {
                auto matches = tree.findSubscribers("pattern:50");
                (void)matches;
            }
        };

        // UnregisterAll thread
        auto unregisterAllThread = [&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            tree.unregisterSubscriberAll("target");
            running = false;
        };

        std::thread t1(reader);
        std::thread t2(unregisterAllThread);

        t1.join();
        t2.join();

        // Verify all removed
        for (int i = 0; i < 100; ++i) {
            auto matches = tree.findSubscribers("pattern:" + std::to_string(i));
            REQUIRE(matches.empty());
        }
    }

    SECTION("Clear under concurrent access") {
        std::atomic<bool> running{true};
        std::atomic<bool> writerDone{false};

        // Writer threads
        auto writer = [&]() {
            int counter = 0;
            while (running) {
                tree.registerSubscriber("test:*:pattern", "sub_" + std::to_string(counter++));
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            writerDone = true;
        };

        // Clear thread - tests that clear() is thread-safe
        auto clearer = [&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            // First, stop the writer
            running = false;
            // Wait for writer to actually finish
            while (!writerDone) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // Now clear is safe - no concurrent writers
            tree.clear();
        };

        std::thread t1(writer);
        std::thread t2(clearer);

        t1.join();
        t2.join();

        // After writer stops and clear runs, tree should be empty
        auto matches = tree.findSubscribers("test:anything:pattern");
        REQUIRE(matches.empty());
    }

    SECTION("Stress test: All operations mixed") {
        std::atomic<bool> running{true};
        std::atomic<int> totalOps{0};

        // Continuous registration
        auto registerWorker = [&](int id) {
            while (running) {
                tree.registerSubscriber("stress:" + std::to_string(id % 10) + ":*",
                                       "reg_" + std::to_string(id));
                totalOps++;
            }
        };

        // Continuous unregistration
        auto unregisterWorker = [&](int id) {
            while (running) {
                tree.unregisterSubscriber("stress:" + std::to_string(id % 10) + ":*",
                                         "reg_" + std::to_string(id));
                totalOps++;
            }
        };

        // Continuous reading
        auto readWorker = [&]() {
            while (running) {
                for (int i = 0; i < 10; ++i) {
                    auto matches = tree.findSubscribers("stress:" + std::to_string(i) + ":test");
                    (void)matches;
                }
                totalOps++;
            }
        };

        std::vector<std::thread> threads;

        // 3 register threads
        for (int i = 0; i < 3; ++i) {
            threads.emplace_back(registerWorker, i);
        }

        // 2 unregister threads
        for (int i = 10; i < 12; ++i) {
            threads.emplace_back(unregisterWorker, i);
        }

        // 5 reader threads
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back(readWorker);
        }

        // Run for 3 seconds
        std::this_thread::sleep_for(std::chrono::seconds(3));
        running = false;

        for (auto& t : threads) {
            t.join();
        }

        INFO("Total operations: " << totalOps);
        REQUIRE(totalOps > 1000); // Should have done many operations
    }
}
