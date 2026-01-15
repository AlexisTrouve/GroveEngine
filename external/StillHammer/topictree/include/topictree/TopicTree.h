#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>         // For unique_lock, shared_lock
#include <shared_mutex>  // For shared_mutex (C++17)
#include <algorithm>

namespace topictree {

/**
 * Ultra-fast Topic Tree for O(k) topic matching (k = topic depth)
 *
 * Replaces O(n*m) regex matching with hierarchical hash map lookup.
 * Supports wildcards: * and .* (equivalent)
 *
 * Example:
 *   pattern: "player:*:position" matches "player:123:position"
 *   pattern: "player:.*" matches "player:123", "player:456:health"
 *
 * Performance: Zero-copy parsing with string_view, cache-friendly layout
 * Thread-safety: Read-write lock for concurrent access
 */
template<typename SubscriberType = std::string>
class TopicTree {
public:
    static constexpr char SEPARATOR = ':';
    static constexpr std::string_view WILDCARD_SINGLE = "*";
    static constexpr std::string_view WILDCARD_MULTI = ".*";

private:
    struct Node {
        // Subscribers at this exact node (for patterns ending here)
        std::unordered_set<SubscriberType> subscribers;

        // Children nodes - exact matches
        std::unordered_map<std::string, std::unique_ptr<Node>> children;

        // Wildcard children - special nodes
        std::unique_ptr<Node> wildcardSingle;   // matches one segment (*)
        std::unique_ptr<Node> wildcardMulti;    // matches rest of path (.*)

        Node() = default;

        // Prevent copies, allow moves
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        Node(Node&&) = default;
        Node& operator=(Node&&) = default;
    };

    Node root;
    mutable std::shared_mutex treeMutex;  // Reader-writer lock for concurrent reads

    // Fast topic splitting - zero-copy with string_view
    static std::vector<std::string_view> splitTopic(std::string_view topic) {
        std::vector<std::string_view> segments;
        segments.reserve(8);  // Pre-allocate for typical depth

        size_t start = 0;
        size_t pos = 0;

        while (pos <= topic.size()) {
            if (pos == topic.size() || topic[pos] == SEPARATOR) {
                if (pos > start) {  // Avoid empty segments
                    segments.push_back(topic.substr(start, pos - start));
                }
                start = pos + 1;
            }
            ++pos;
        }

        return segments;
    }

    // Recursive pattern insertion
    void insertPattern(Node* node, const std::vector<std::string_view>& segments,
                      size_t index, const SubscriberType& subscriber) {
        // End of pattern - add subscriber here
        if (index >= segments.size()) {
            node->subscribers.insert(subscriber);
            return;
        }

        std::string_view segment = segments[index];

        // Check for multi-level wildcard (.*)
        if (segment == WILDCARD_MULTI) {
            if (!node->wildcardMulti) {
                node->wildcardMulti = std::make_unique<Node>();
            }
            // .* matches everything from here - subscriber at this node
            node->wildcardMulti->subscribers.insert(subscriber);
            return;
        }

        // Check for single-level wildcard (*)
        if (segment == WILDCARD_SINGLE) {
            if (!node->wildcardSingle) {
                node->wildcardSingle = std::make_unique<Node>();
            }
            insertPattern(node->wildcardSingle.get(), segments, index + 1, subscriber);
            return;
        }

        // Exact match - convert to std::string for map key
        std::string segmentStr(segment);
        auto& child = node->children[segmentStr];
        if (!child) {
            child = std::make_unique<Node>();
        }
        insertPattern(child.get(), segments, index + 1, subscriber);
    }

    // Recursive pattern matching - collect all matching subscribers
    void findMatches(const Node* node, const std::vector<std::string_view>& segments,
                    size_t index, std::unordered_set<SubscriberType>& matches) const {
        if (!node) return;

        // If we've consumed all segments, collect subscribers at this node
        if (index >= segments.size()) {
            matches.insert(node->subscribers.begin(), node->subscribers.end());
            return;
        }

        std::string_view segment = segments[index];

        // 1. Check exact match
        std::string segmentStr(segment);
        auto it = node->children.find(segmentStr);
        if (it != node->children.end()) {
            findMatches(it->second.get(), segments, index + 1, matches);
        }

        // 2. Check single wildcard (matches this segment)
        if (node->wildcardSingle) {
            findMatches(node->wildcardSingle.get(), segments, index + 1, matches);
        }

        // 3. Check multi-level wildcard (matches rest of topic)
        if (node->wildcardMulti) {
            matches.insert(node->wildcardMulti->subscribers.begin(),
                          node->wildcardMulti->subscribers.end());
        }
    }

    // Recursive subscriber removal
    bool removeSubscriberFromNode(Node* node, const std::vector<std::string_view>& segments,
                                 size_t index, const SubscriberType& subscriber) {
        if (!node) return false;

        // End of pattern - remove from this node
        if (index >= segments.size()) {
            node->subscribers.erase(subscriber);
            // Return true if node is now empty (for cleanup)
            return node->subscribers.empty() &&
                   node->children.empty() &&
                   !node->wildcardSingle &&
                   !node->wildcardMulti;
        }

        std::string_view segment = segments[index];

        // Multi-level wildcard
        if (segment == WILDCARD_MULTI) {
            if (node->wildcardMulti) {
                node->wildcardMulti->subscribers.erase(subscriber);
                if (node->wildcardMulti->subscribers.empty()) {
                    node->wildcardMulti.reset();
                }
            }
            return false;
        }

        // Single wildcard
        if (segment == WILDCARD_SINGLE) {
            if (node->wildcardSingle) {
                bool isEmpty = removeSubscriberFromNode(node->wildcardSingle.get(),
                                                       segments, index + 1, subscriber);
                if (isEmpty) {
                    node->wildcardSingle.reset();
                }
            }
            return false;
        }

        // Exact match
        std::string segmentStr(segment);
        auto it = node->children.find(segmentStr);
        if (it != node->children.end()) {
            bool isEmpty = removeSubscriberFromNode(it->second.get(),
                                                   segments, index + 1, subscriber);
            if (isEmpty) {
                node->children.erase(it);
            }
        }

        // Check if this node is now empty
        return node->subscribers.empty() &&
               node->children.empty() &&
               !node->wildcardSingle &&
               !node->wildcardMulti;
    }

public:
    TopicTree() = default;

    /**
     * Register a subscriber for a topic pattern
     *
     * @param pattern Topic pattern with optional wildcards (e.g., "player:*:position")
     * @param subscriber Subscriber ID/object
     *
     * Complexity: O(k) where k = pattern depth
     */
    void registerSubscriber(const std::string& pattern, const SubscriberType& subscriber) {
        auto segments = splitTopic(pattern);

        std::unique_lock lock(treeMutex);  // WRITE - exclusive lock
        insertPattern(&root, segments, 0, subscriber);
    }

    /**
     * Find all subscribers matching a topic
     *
     * @param topic Concrete topic (e.g., "player:123:position")
     * @return Vector of all matching subscribers
     *
     * Complexity: O(k) where k = topic depth
     */
    std::vector<SubscriberType> findSubscribers(const std::string& topic) const {
        auto segments = splitTopic(topic);

        std::unordered_set<SubscriberType> matches;

        std::shared_lock lock(treeMutex);  // READ - concurrent access allowed!
        findMatches(&root, segments, 0, matches);

        return std::vector<SubscriberType>(matches.begin(), matches.end());
    }

    /**
     * Unregister a subscriber from a specific pattern
     *
     * @param pattern Topic pattern
     * @param subscriber Subscriber to remove
     *
     * Complexity: O(k) where k = pattern depth
     */
    void unregisterSubscriber(const std::string& pattern, const SubscriberType& subscriber) {
        auto segments = splitTopic(pattern);

        std::unique_lock lock(treeMutex);  // WRITE - exclusive lock
        removeSubscriberFromNode(&root, segments, 0, subscriber);
    }

    /**
     * Remove subscriber from ALL patterns
     *
     * Note: This requires full tree traversal - O(n) where n = total nodes
     * Use sparingly, prefer unregisterSubscriber with specific pattern
     */
    void unregisterSubscriberAll(const SubscriberType& subscriber) {
        std::unique_lock lock(treeMutex);  // WRITE - exclusive lock
        unregisterSubscriberAllRecursive(&root, subscriber);
    }

    /**
     * Clear all subscriptions
     */
    void clear() {
        std::unique_lock lock(treeMutex);  // WRITE - exclusive lock
        root = Node();
    }

    /**
     * Get total number of subscribers (may count duplicates across patterns)
     */
    size_t subscriberCount() const {
        std::shared_lock lock(treeMutex);  // READ - concurrent access allowed!
        return countSubscribersRecursive(&root);
    }

private:
    void unregisterSubscriberAllRecursive(Node* node, const SubscriberType& subscriber) {
        if (!node) return;

        node->subscribers.erase(subscriber);

        for (auto& [key, child] : node->children) {
            unregisterSubscriberAllRecursive(child.get(), subscriber);
        }

        if (node->wildcardSingle) {
            unregisterSubscriberAllRecursive(node->wildcardSingle.get(), subscriber);
        }

        if (node->wildcardMulti) {
            unregisterSubscriberAllRecursive(node->wildcardMulti.get(), subscriber);
        }
    }

    size_t countSubscribersRecursive(const Node* node) const {
        if (!node) return 0;

        size_t count = node->subscribers.size();

        for (const auto& [key, child] : node->children) {
            count += countSubscribersRecursive(child.get());
        }

        if (node->wildcardSingle) {
            count += countSubscribersRecursive(node->wildcardSingle.get());
        }

        if (node->wildcardMulti) {
            count += countSubscribersRecursive(node->wildcardMulti.get());
        }

        return count;
    }
};

} // namespace topictree
