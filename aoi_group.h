#ifndef __AOI_GROUP_H__
#define __AOI_GROUP_H__

#include "zeeset.h"

#include <cassert>
#include <functional>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

struct AOI_EVENT_IDS {
    static constexpr int ENTER = -1;
    static constexpr int LEAVE = -2;
    static constexpr int MOVE = -3;
};

struct AOI_WATCH_TYPES {
    static constexpr int WATCHER = 1;
    static constexpr int MAKER = 2;
    static constexpr int BOTH = 3;
};

const char *AoiEventIdRepr(int event) {
    switch(event) {
        case AOI_EVENT_IDS::ENTER:
            return "ENTER";
        case AOI_EVENT_IDS::LEAVE:
            return "LEAVE";
        case AOI_EVENT_IDS::MOVE:
            return "MOVE";
        default:
            return "UNKNOWN";
    }
}

template<typename KeyType, typename PosType, int Dimension>
class AoiEventType {
public:
    using KEY_TYPE = KeyType;
    using POS_TYPE = PosType;
    static constexpr int DIMENSION = Dimension;

    int EVENT_ID = 0;
    POS_TYPE POS[DIMENSION];
    POS_TYPE POS_FROM[DIMENSION];
    void *USERDATA = NULL;
};

template<typename KeyType, typename PosType, int Dimension>
class AoiGroup {
public:
    using KEY_TYPE = KeyType;
    using POS_TYPE = PosType;
    static constexpr int DIMENSION = Dimension;
    static constexpr POS_TYPE POS_ZERO = (POS_TYPE)0;

    using AOI_EVENT_TYPE = AoiEventType<KEY_TYPE, POS_TYPE, DIMENSION>;
    using EVENT_CALLBACK = typename std::function<void(const KEY_TYPE &receiver, const KEY_TYPE &sender, const AOI_EVENT_TYPE &event)>;

private:
    EVENT_CALLBACK m_eventcb = NULL;
    POS_TYPE m_max_watch_range[DIMENSION];

    struct ElementType {
        int WATCH_TYPE;
        POS_TYPE POS[DIMENSION];
        POS_TYPE WATCH_RANGE[DIMENSION];

        std::unordered_set<KEY_TYPE> RELATED_WATCHERS;
        std::unordered_set<KEY_TYPE> RELATED_MAKERS;
    };
    std::unordered_map<KEY_TYPE, ElementType> m_elements;

    struct DimensionType {
        ZeeSkiplist<KEY_TYPE, POS_TYPE> WATCHER_LOWER_LIST;
        ZeeSkiplist<KEY_TYPE, POS_TYPE> WATCHER_UPPER_LIST;
        ZeeSkiplist<KEY_TYPE, POS_TYPE> MAKER_LIST;
    };
    DimensionType m_dimensions[DIMENSION];

public:
    AoiGroup(const POS_TYPE max_watch_range[DIMENSION]) {
        for(int i = 0; i < DIMENSION; ++i) {
            assert(POS_ZERO < max_watch_range[i]);
        }

        CopyPos(max_watch_range, m_max_watch_range);
    }

    void SetCallback(EVENT_CALLBACK cb) {
        m_eventcb = cb;
    }

    bool Enter(const KEY_TYPE &key, const POS_TYPE pos[DIMENSION], int watch_type, const POS_TYPE watch_range[DIMENSION]) {
        if(m_elements.count(key)) {
            return false;
        }

        ElementType &element = m_elements[key];

        element.WATCH_TYPE = watch_type;
        CopyPos(pos, element.POS);
        CopyPos(watch_range, element.WATCH_RANGE);
        TrimWatchRange(element.WATCH_RANGE);

        if(watch_type & AOI_WATCH_TYPES::MAKER) {
            InsertMaker(key, element);
        }

        if(watch_type & AOI_WATCH_TYPES::WATCHER) {
            InsertWatcher(key, element);
        }

        return true;
    }

    bool Enter(const KEY_TYPE &key, const POS_TYPE pos[DIMENSION], int watch_type) {
        POS_TYPE range[DIMENSION] = { POS_ZERO };

        return Enter(key, pos, watch_type, range);
    }

    bool Leave(const KEY_TYPE &key) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType element = iter->second;
        m_elements.erase(iter);

        int watch_type = element.WATCH_TYPE;
        if(watch_type & AOI_WATCH_TYPES::MAKER) {
            RemoveMaker(key, element);
        }

        if(watch_type & AOI_WATCH_TYPES::WATCHER) {
            RemoveWatcher(key, element);
        }

        return true;
    }

    bool Move(const KEY_TYPE &key, const POS_TYPE pos[DIMENSION]) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        if(IsSamePos(element.POS, pos)) {
            return true;
        }

        ElementType old_element = element;
        CopyPos(pos, element.POS);

        int watch_type = element.WATCH_TYPE;
        if(watch_type & AOI_WATCH_TYPES::MAKER) {
            UpdateMaker(key, element, old_element);
        }

        if(watch_type & AOI_WATCH_TYPES::WATCHER) {
            UpdateWatcher(key, element, old_element);
        }

        return true;
    }

    bool ChangeWatchType(const KEY_TYPE &key, int watch_type) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        int old_watch_type = element.WATCH_TYPE;
        element.WATCH_TYPE = watch_type;

        bool old_is_watcher = (old_watch_type & AOI_WATCH_TYPES::WATCHER) != 0;
        bool old_is_maker = (old_watch_type & AOI_WATCH_TYPES::MAKER) != 0;
        bool new_is_watcher = (watch_type & AOI_WATCH_TYPES::WATCHER) != 0;
        bool new_is_maker = (watch_type & AOI_WATCH_TYPES::MAKER) != 0;

        if(old_is_maker && !new_is_maker) {
            RemoveMaker(key, element);
        }

        if(!old_is_maker && new_is_maker) {
            InsertMaker(key, element);
        }

        if(old_is_watcher && !new_is_watcher) {
            RemoveWatcher(key, element);
        }

        if(!old_is_watcher && new_is_watcher) {
            InsertWatcher(key, element);
        }

        return true;
    }

    bool ChangeWatchRange(const KEY_TYPE &key, const POS_TYPE watch_range[DIMENSION]) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        POS_TYPE watch_range_mutable[DIMENSION];
        CopyPos(watch_range, watch_range_mutable);
        TrimWatchRange(watch_range_mutable);

        if(IsSamePos(element.WATCH_RANGE, watch_range_mutable)) {
            return true;
        }

        ElementType old_element = element;
        CopyPos(watch_range_mutable, element.WATCH_RANGE);

        int watch_type = element.WATCH_TYPE;
        if(watch_type & AOI_WATCH_TYPES::WATCHER) {
            UpdateWatcher(key, element, old_element);
        }

        return false;
    }

    bool GetElementPosition(const KEY_TYPE &key, POS_TYPE pos[DIMENSION]) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        CopyPos(element.POS, pos);

        return true;
    }

    bool BroadcastEventToWatchers(const KEY_TYPE &key, const AOI_EVENT_TYPE &event) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        std::vector<KEY_TYPE> related_watchers(element.RELATED_WATCHERS.begin(), element.RELATED_WATCHERS.end());

        for(const KEY_TYPE &watcher: related_watchers) {
            Callback(watcher, key, event);
        }

        return true;
    }

    // get whom can see i
    bool GetWatchersList(const KEY_TYPE &key, std::vector<KEY_TYPE> &watchers) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        watchers.assign(element.RELATED_WATCHERS.begin(), element.RELATED_WATCHERS.end());

        return true;
    }

    // get whom i can see
    bool GetMakersList(const KEY_TYPE &key, std::vector<KEY_TYPE> &makers) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        makers.assign(element.RELATED_MAKERS.begin(), element.RELATED_MAKERS.end());

        return true;
    }

    void GetMakersInRange(const POS_TYPE pos[DIMENSION], const POS_TYPE range[DIMENSION], std::vector<KEY_TYPE> &makers, const KEY_TYPE *excludes_sorted = NULL, size_t excludes_size = 0) {
        static_assert(DIMENSION > 0);
        
        assert(std::is_sorted(excludes_sorted, excludes_sorted + excludes_size));
        makers.clear();

        // 找到几个维度里面，落在区间内的maker数量最少的那个维度，减少后续筛选的数量
        int target_dimension = -1;
        unsigned long target_count = 0;

        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = pos[i] - range[i];
            POS_TYPE upper = pos[i] + range[i];

            unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(lower, false, upper, false);

            if(target_dimension < 0 || count < target_count) {
                target_dimension = i;
                target_count = count;
            } 
        }

        assert(target_dimension >= 0);

        // 遍历维度 target_dimension，进行筛选
        {
            int i = target_dimension;
            POS_TYPE lower = pos[i] - range[i];
            POS_TYPE upper = pos[i] + range[i];

            m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(lower, false, upper, false,
                    [&makers, excludes_sorted, excludes_size, this, pos, range](unsigned long _0, const KEY_TYPE &key, const POS_TYPE &_1) {
                        if(excludes_size && std::binary_search(excludes_sorted, excludes_sorted + excludes_size, key)) {
                            return;
                        }

                        // 检查key是否在范围内
                        auto iter = this->m_elements.find(key);
                        if(iter == this->m_elements.end()) {
                            return;
                        }

                        ElementType &e = iter->second;

                        for(int k = 0; k < DIMENSION; ++k) {
                            POS_TYPE lo = pos[k] - range[k];
                            POS_TYPE up = pos[k] + range[k];

                            if( !(lo < e.POS[k]) || !(e.POS[k] < up) ) {
                                return;
                            }
                        }

                        makers.emplace_back(key);

                    });
        }
    }

    void GetWatchersRelatedToPos(const POS_TYPE pos[DIMENSION], std::vector<KEY_TYPE> &watchers, const KEY_TYPE *excludes_sorted = NULL, size_t excludes_size = 0) {
        static_assert(DIMENSION > 0);
        assert(std::is_sorted(excludes_sorted, excludes_sorted + excludes_size));

        watchers.clear();

        // 找到几个维度里，落在搜索区间数量最少的维度
        int target_dimension = -1;
        unsigned long target_count = 0;
        bool use_lower = true;

        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower_begin = pos[i] - m_max_watch_range[i];
            POS_TYPE lower_end = pos[i];

            unsigned long count = m_dimensions[i].WATCHER_LOWER_LIST.GetElementsCountByRangedValue(lower_begin, false, lower_end, false);

            if(target_dimension < 0 || count < target_count) {
                target_dimension = i;
                target_count = count;
                use_lower = true;
            }

            POS_TYPE upper_begin = pos[i];
            POS_TYPE upper_end = pos[i] + m_max_watch_range[i];

            count = m_dimensions[i].WATCHER_UPPER_LIST.GetElementsCountByRangedValue(upper_begin, false, upper_end, false);

            if(count < target_count) {
                target_dimension = i;
                target_count = count;
                use_lower = false;
            }
        }

        assert(target_dimension >= 0);

        {
            int i = target_dimension;

            auto cb = [&watchers, excludes_sorted, excludes_size, this, pos](unsigned long _0, const KEY_TYPE &key, const POS_TYPE &_1) {
                if(excludes_size && std::binary_search(excludes_sorted, excludes_sorted + excludes_size, key)) {
                    return;
                }

                // 检查key能否观察到pos
                auto iter = this->m_elements.find(key);
                if(iter == this->m_elements.end()) {
                    return;
                }

                ElementType &e = iter->second;

                for(int k = 0; k < DIMENSION; ++k) {
                    POS_TYPE lower = e.POS[k] - e.WATCH_RANGE[k];
                    POS_TYPE upper = e.POS[k] + e.WATCH_RANGE[k];

                    if( !(lower < pos[k]) ||  !(pos[k] < upper) ) {
                        return;
                    }
                }

                watchers.emplace_back(key);
            };

            if(use_lower) {
                POS_TYPE lower_begin = pos[i] - m_max_watch_range[i];
                POS_TYPE lower_end = pos[i];
                m_dimensions[i].WATCHER_LOWER_LIST.GetElementsByRangedValue(lower_begin, false, lower_end, false, cb);
            } else {
                POS_TYPE upper_begin = pos[i];
                POS_TYPE upper_end = pos[i] + m_max_watch_range[i];
                m_dimensions[i].WATCHER_UPPER_LIST.GetElementsByRangedValue(upper_begin, false, upper_end, false, cb);
            }
        }

    }

    void BroadcastEventToWatchersByPos(POS_TYPE pos[DIMENSION], const KEY_TYPE &sender, const AOI_EVENT_TYPE &event) {
        std::vector<KEY_TYPE> &watchers;
        GetWatchersRelatedToPos(pos, watchers);

        for(const KEY_TYPE &watcher: watchers) {
            Callback(watcher, sender, event);
        }
    }

    std::string DumpElements() {
        std::ostringstream ss;

        ss << "** DUMP ELEMENTS BEGIN\n";
        for(auto iter = m_elements.begin(); iter != m_elements.end(); ++iter) {
            ss << "ID=" << iter->first << ": ";
            const ElementType &element = iter->second;

            ss << "POS=(";
            for(int i = 0; i < DIMENSION; ++i) {
                if(i != 0) {
                    ss << ",";
                }

                ss << element.POS[i];
            }
            ss << ") ";

            if(element.WATCH_TYPE & AOI_WATCH_TYPES::WATCHER) {
                ss << "<W> ";
                ss << "WATCH_RANGE=(";
                for(int i = 0; i < DIMENSION; ++i) {
                    if(i != 0) {
                        ss << ",";
                    }

                    ss << element.WATCH_RANGE[i];
                }
                ss << ") ";

                ss << "RELATED_MAKERS=(";
                for(const KEY_TYPE &key: element.RELATED_MAKERS) {
                    ss << key << ",";
                }
                ss << ") ";
            }

            if(element.WATCH_TYPE & AOI_WATCH_TYPES::MAKER) {
                ss << "<M> ";
                ss << "RELATED_WATCHERS=(";
                for(const KEY_TYPE &key: element.RELATED_WATCHERS) {
                    ss << key << ",";
                }
                ss << ") ";
            }

            ss << "\n";
        }
        ss << "** DUMP ELEMENTS END";

        return ss.str();
    }

    std::string DumpSlist() {
        std::ostringstream ss;
        ss << "** DUMP SLIST BEGIN\n";
        for(int i = 0; i < DIMENSION; ++i) {
            ss << "*** DUMP dimension #" << i << " WATCHER_LOWER_LIST BEGIN\n";
            ss << m_dimensions[i].WATCHER_LOWER_LIST.DumpLevels() << "\n";
            ss << "*** DUMP dimension #" << i << " WATCHER_LOWER_LIST END\n";

            ss << "*** DUMP dimension #" << i << " WATCHER_UPPER_LIST BEGIN\n";
            ss << m_dimensions[i].WATCHER_UPPER_LIST.DumpLevels() << "\n";
            ss << "*** DUMP dimension #" << i << " WATCHER_UPPER_LIST END\n";

            ss << "*** DUMP dimension #" << i << " MAKER_LIST BEGIN\n";
            ss << m_dimensions[i].MAKER_LIST.DumpLevels() << "\n";
            ss << "*** DUMP dimension #" << i << " MAKER_LIST END\n";
        }
        ss << "** DUMP SLIST END";
        return ss.str();
    }

private:
    void Callback(const KEY_TYPE &receiver, const KEY_TYPE &sender, const AOI_EVENT_TYPE &event) {
        if(m_eventcb) {
            m_eventcb(receiver, sender, event);
        }
    }

    void CopyPos(const POS_TYPE src[DIMENSION], POS_TYPE dst[DIMENSION]) {
        std::copy(src, src + DIMENSION, dst);
    }

    bool IsSamePos(const POS_TYPE x[DIMENSION], const POS_TYPE y[DIMENSION]) {
        return std::equal(x, x + DIMENSION, y);
    }

    void TrimWatchRange(POS_TYPE watch_range[DIMENSION]) {
        for(int i = 0; i < DIMENSION; ++i) {
            if(watch_range[i] < POS_ZERO) {
                watch_range[i] = POS_ZERO;
            } else if(m_max_watch_range[i] < watch_range[i]) {
                watch_range[i] = m_max_watch_range[i];
            }
        }
    }

    void InsertWatcher(const KEY_TYPE &key, ElementType &element) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
            POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

            m_dimensions[i].WATCHER_LOWER_LIST.Insert(key, lower);
            m_dimensions[i].WATCHER_UPPER_LIST.Insert(key, upper);
        }

        std::vector<KEY_TYPE> makers;

        GetMakersInRange(element.POS, element.WATCH_RANGE, makers, &key, 1); // 排除自己，不观察自己

        for(const KEY_TYPE &maker: makers) {
            auto iter = m_elements.find(maker);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &maker_element = iter->second;
            maker_element.RELATED_WATCHERS.insert(key);

            element.RELATED_MAKERS.insert(maker);
        }

        if(makers.size()) {
            AOI_EVENT_TYPE event;
            event.EVENT_ID = AOI_EVENT_IDS::ENTER;
            
            for(const KEY_TYPE &maker: makers) {
                auto iter = m_elements.find(maker);

                if(iter == m_elements.end()) {
                    continue;
                }

                ElementType &maker_element = iter->second;

                CopyPos(maker_element.POS, event.POS);

                // 通知本watcher，周围所有的maker已进入
                Callback(key, maker, event);
            }
        }
    }

    void InsertMaker(const KEY_TYPE &key, ElementType &element) {
        for(int i = 0; i < DIMENSION; ++i) {
            m_dimensions[i].MAKER_LIST.Insert(key, element.POS[i]);
        }

        std::vector<KEY_TYPE> watchers;

        GetWatchersRelatedToPos(element.POS, watchers, &key, 1); // 排除自己，不被自己观察

        for(const KEY_TYPE &watcher: watchers) {
            auto iter = m_elements.find(watcher);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &watcher_element = iter->second;
            watcher_element.RELATED_MAKERS.insert(key);

            element.RELATED_WATCHERS.insert(watcher);
        }

        if(watchers.size()) {
            AOI_EVENT_TYPE event;
            event.EVENT_ID = AOI_EVENT_IDS::ENTER;
            CopyPos(element.POS, event.POS);

            for(const KEY_TYPE &watcher: watchers) {
                // 通知周围的watcher,本maker已进入
                Callback(watcher, key, event);
            }
        }
    }

    void UpdateWatcher(const KEY_TYPE &key, ElementType &element, const ElementType &old_element) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
            POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

            POS_TYPE old_lower = old_element.POS[i] - old_element.WATCH_RANGE[i];
            POS_TYPE old_upper = old_element.POS[i] + old_element.WATCH_RANGE[i];

            m_dimensions[i].WATCHER_LOWER_LIST.Update(key, old_lower, lower);
            m_dimensions[i].WATCHER_UPPER_LIST.Update(key, old_upper, upper);
        }

        std::vector<KEY_TYPE> new_makers;

        GetMakersInRange(element.POS, element.WATCH_RANGE, new_makers, &key, 1); // 排除自己，不观察自己
        std::sort(new_makers.begin(), new_makers.end());

        
        std::vector<KEY_TYPE> leave_makers;
        std::vector<KEY_TYPE> keep_makers;
        std::vector<KEY_TYPE> enter_makers;

        std::vector<KEY_TYPE> old_makers(old_element.RELATED_MAKERS.begin(), old_element.RELATED_MAKERS.end());
        std::sort(old_makers.begin(), old_makers.end());

        // new_makers和old_makers都是有序的

        DiffSortedKeylist(leave_makers, keep_makers, enter_makers, old_makers, new_makers);

        for(const KEY_TYPE &maker: leave_makers) {
            element.RELATED_MAKERS.erase(maker);

            auto iter = m_elements.find(maker);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &maker_element = iter->second;

            maker_element.RELATED_WATCHERS.erase(key);
        }

        for(const KEY_TYPE &maker: enter_makers) {
            element.RELATED_MAKERS.insert(maker);

            auto iter = m_elements.find(maker);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &maker_element = iter->second;

            maker_element.RELATED_WATCHERS.insert(key);
        }

        if(leave_makers.size() || enter_makers.size()) {
            AOI_EVENT_TYPE event;

            event.EVENT_ID = AOI_EVENT_IDS::LEAVE;
            for(const KEY_TYPE &maker: leave_makers) {
                // 通知本watcher，该maker已离开
                auto iter = m_elements.find(maker);

                if(iter == m_elements.end()) {
                    continue;
                }

                ElementType &maker_element = iter->second;
                CopyPos(maker_element.POS, event.POS);
                Callback(key, maker, event);
            }

            event.EVENT_ID = AOI_EVENT_IDS::ENTER;
            for(const KEY_TYPE &maker: enter_makers) {
                // 通知本watcher，该maker已进入
                auto iter = m_elements.find(maker);

                if(iter == m_elements.end()) {
                    continue;
                }

                ElementType &maker_element = iter->second;
                CopyPos(maker_element.POS, event.POS);
                Callback(key, maker, event);
            }
            
            // 移动watcher不发送移动的消息
        }
    }

    void UpdateMaker(const KEY_TYPE &key, ElementType &element, const ElementType &old_element) {
        for(int i = 0; i < DIMENSION; ++i) {
            m_dimensions[i].MAKER_LIST.Update(key, old_element.POS[i], element.POS[i]);
        }

        std::vector<KEY_TYPE> new_watchers;

        GetWatchersRelatedToPos(element.POS, new_watchers, &key, 1); // 排除自己，不被自己观察
        std::sort(new_watchers.begin(), new_watchers.end());

        std::vector<KEY_TYPE> leave_watchers;
        std::vector<KEY_TYPE> keep_watchers;
        std::vector<KEY_TYPE> enter_watchers;

        std::vector<KEY_TYPE> old_watchers(old_element.RELATED_WATCHERS.begin(), old_element.RELATED_WATCHERS.end());
        std::sort(old_watchers.begin(), old_watchers.end());

        // 这里 old_watchers 和 new_watchers 都是有序的，不需要再排序
        // 如果改动了代码，导致无序，那么需要在这里进行排序

        DiffSortedKeylist(leave_watchers, keep_watchers, enter_watchers, old_watchers, new_watchers);

        for(const KEY_TYPE &watcher: leave_watchers) {
            element.RELATED_WATCHERS.erase(watcher);

            auto iter = m_elements.find(watcher);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &watcher_element = iter->second;

            watcher_element.RELATED_MAKERS.erase(key);
        }

        for(const KEY_TYPE &watcher: enter_watchers) {
            element.RELATED_WATCHERS.insert(watcher);

            auto iter = m_elements.find(watcher);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &watcher_element = iter->second;

            watcher_element.RELATED_MAKERS.insert(key);
        }

        if(leave_watchers.size() || keep_watchers.size() || enter_watchers.size()) {
            AOI_EVENT_TYPE event;

            CopyPos(element.POS, event.POS);
            CopyPos(old_element.POS, event.POS_FROM);

            event.EVENT_ID = AOI_EVENT_IDS::LEAVE;
            for(const KEY_TYPE &watcher: leave_watchers) {
                // 通知watcher，本maker已离开
                Callback(watcher, key, event);
            }

            event.EVENT_ID = AOI_EVENT_IDS::MOVE;
            for(const KEY_TYPE &watcher: keep_watchers) {
                // 通知watcher移动信息
                Callback(watcher, key, event);
            }

            event.EVENT_ID = AOI_EVENT_IDS::ENTER;
            for(const KEY_TYPE &watcher: enter_watchers) {
                // 通知watcher，本maker已进入
                Callback(watcher, key, event);
            }
        }
    }

    void RemoveWatcher(const KEY_TYPE &key, ElementType &element) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
            POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

            m_dimensions[i].WATCHER_LOWER_LIST.Delete(key, lower);
            m_dimensions[i].WATCHER_UPPER_LIST.Delete(key, upper);
        }

        for(const KEY_TYPE &maker: element.RELATED_MAKERS) {
            auto iter = m_elements.find(maker);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &maker_element = iter->second;

            maker_element.RELATED_WATCHERS.erase(key);
        }

        element.RELATED_MAKERS.clear();

        // 移除watcher不产生任何事件
    }

    void RemoveMaker(const KEY_TYPE &key, ElementType &element) {
        for(int i = 0; i < DIMENSION; ++i) {
            m_dimensions[i].MAKER_LIST.Delete(key, element.POS[i]);
        }

        for(const KEY_TYPE &watcher: element.RELATED_WATCHERS) {
            auto iter = m_elements.find(watcher);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &watcher_element = iter->second;

            watcher_element.RELATED_MAKERS.erase(key);
        }

        if(element.RELATED_WATCHERS.size()) {
            AOI_EVENT_TYPE event;
            event.EVENT_ID = AOI_EVENT_IDS::LEAVE;
            CopyPos(element.POS, event.POS);

            std::vector<KEY_TYPE> watchers(element.RELATED_WATCHERS.begin(), element.RELATED_WATCHERS.end());
            element.RELATED_WATCHERS.clear();

            for(const KEY_TYPE &watcher: watchers) {
                // 通知周围的watcher，本maker已离开
                Callback(watcher, key, event);
            }
        }
    }

    void DiffSortedKeylist(std::vector<KEY_TYPE> &leaves, std::vector<KEY_TYPE> &keeps, 
            std::vector<KEY_TYPE> &enters, const std::vector<KEY_TYPE> &old, const std::vector<KEY_TYPE> &newl) {
        size_t oldlen = old.size();
        size_t newlen = newl.size();

        size_t oldid = 0, newid = 0;

        while(oldid < oldlen && newid < newlen) {
            if(old[oldid] == newl[newid]) {
                keeps.emplace_back(old[oldid]);
                ++oldid;
                ++newid;
            } else if(old[oldid] < newl[newid]) {
                leaves.emplace_back(old[oldid]);
                ++oldid;
            } else {
                enters.emplace_back(newl[newid]);
                ++newid;
            }
        }

        while(oldid < oldlen) {
            leaves.emplace_back(old[oldid]);
            ++oldid;
        }

        while(newid < newlen) {
            enters.emplace_back(newl[newid]);
            ++newid;
        }
    }
};

#endif
