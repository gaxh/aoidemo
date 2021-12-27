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

    struct GetMakersInRangeHint {
        int TARGET_DIMENSION;
        unsigned long COMPLEXITY;
    };

    struct GetWatchersRelatedToPosHint {
        int TARGET_DIMENSION;
        unsigned long COMPLEXITY;
        bool USE_LOWER;
    };

    struct MoveWatcherHint {
        int LEAVE_DIMENSION[DIMENSION];
        int ENTER_DIMENSION[DIMENSION];
        unsigned long COMPLEXITY;
    };

    struct MoveMakerHint {
        int LEAVE_DIMENSION[DIMENSION];
        int ENTER_DIMENSION[DIMENSION];
        bool LEAVE_USE_LOWER[DIMENSION];
        bool ENTER_USE_LOWER[DIMENSION];
        unsigned long COMPLEXITY;
    };

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
            MoveMaker(key, element, old_element);
        }

        if(watch_type & AOI_WATCH_TYPES::WATCHER) {
            MoveWatcher(key, element, old_element);
        }

        return true;
    }

    bool MoveDiff(const KEY_TYPE &key, const POS_TYPE diff[DIMENSION]) {
        auto iter = m_elements.find(key);

        if(iter == m_elements.end()) {
            return false;
        }

        ElementType &element = iter->second;

        POS_TYPE pos[DIMENSION];

        for(int i = 0; i < DIMENSION; ++i) {
            pos[i] = element.POS[i] + diff[i];
        }

        return Move(key, pos);
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

    void CalcGetMakersInRangeHint(const POS_TYPE pos[DIMENSION], const POS_TYPE range[DIMENSION], GetMakersInRangeHint &hint) {
        hint.TARGET_DIMENSION = -1;
        hint.COMPLEXITY = 0;

        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = pos[i] - range[i];
            POS_TYPE upper = pos[i] + range[i];

            unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(lower, false, upper, false);

            if(hint.TARGET_DIMENSION < 0 || count < hint.COMPLEXITY) {
                hint.TARGET_DIMENSION = i;
                hint.COMPLEXITY = count;
            }
        }
    }

    void GetMakersInRange(const POS_TYPE pos[DIMENSION], const POS_TYPE range[DIMENSION], std::vector<KEY_TYPE> &makers, const KEY_TYPE *excludes_sorted = NULL, size_t excludes_size = 0, const GetMakersInRangeHint *hint = NULL) {
        static_assert(DIMENSION > 0);
        assert(std::is_sorted(excludes_sorted, excludes_sorted + excludes_size));
        makers.clear();

        // 找到几个维度里面，落在区间内的maker数量最少的那个维度，减少后续筛选的数量
        GetMakersInRangeHint h;
        if(!hint) {
            CalcGetMakersInRangeHint(pos, range, h);
            hint = &h;
        }

        assert(hint->TARGET_DIMENSION >= 0);

        // 遍历维度 target_dimension，进行筛选
        {
            int i = hint->TARGET_DIMENSION;
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

    void CalcGetWatchersRelatedToPosHint(const POS_TYPE pos[DIMENSION], GetWatchersRelatedToPosHint &hint) {
        hint.TARGET_DIMENSION = -1;
        hint.COMPLEXITY = 0;
        hint.USE_LOWER = true;

        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower_begin = pos[i] - m_max_watch_range[i];
            POS_TYPE lower_end = pos[i];

            unsigned long count = m_dimensions[i].WATCHER_LOWER_LIST.GetElementsCountByRangedValue(lower_begin, false, lower_end, false);

            if(hint.TARGET_DIMENSION < 0 || count < hint.COMPLEXITY) {
                hint.TARGET_DIMENSION = i;
                hint.COMPLEXITY = count;
                hint.USE_LOWER = true;
            }

            POS_TYPE upper_begin = pos[i];
            POS_TYPE upper_end = pos[i] + m_max_watch_range[i];

            count = m_dimensions[i].WATCHER_UPPER_LIST.GetElementsCountByRangedValue(upper_begin, false, upper_end, false);

            if(count < hint.COMPLEXITY) {
                hint.TARGET_DIMENSION = i;
                hint.COMPLEXITY = count;
                hint.USE_LOWER = false;
            }
        }
    }

    void GetWatchersRelatedToPos(const POS_TYPE pos[DIMENSION], std::vector<KEY_TYPE> &watchers, const KEY_TYPE *excludes_sorted = NULL, size_t excludes_size = 0, const GetWatchersRelatedToPosHint *hint = NULL) {
        static_assert(DIMENSION > 0);
        assert(std::is_sorted(excludes_sorted, excludes_sorted + excludes_size));
        watchers.clear();

        // 找到几个维度里，落在搜索区间数量最少的维度
        GetWatchersRelatedToPosHint h;
        if(!hint) {
            CalcGetWatchersRelatedToPosHint(pos, h);
            hint = &h;
        }

        assert(hint->TARGET_DIMENSION >= 0);

        {
            int i = hint->TARGET_DIMENSION;

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

            if(hint->USE_LOWER) {
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

    bool TestSelf() {
        for(auto iter = m_elements.begin(); iter != m_elements.end(); ++iter) {
            const KEY_TYPE &key = iter->first;
            const ElementType &e = iter->second;

            if(e.WATCH_TYPE & AOI_WATCH_TYPES::WATCHER) {
                std::vector<KEY_TYPE> makerlist;
                GetMakersInRange(e.POS, e.WATCH_RANGE, makerlist, &key, 1);

                std::vector<KEY_TYPE> stored_makerlist(e.RELATED_MAKERS.begin(), e.RELATED_MAKERS.end());

                std::sort(makerlist.begin(), makerlist.end());
                std::sort(stored_makerlist.begin(), stored_makerlist.end());

                if(makerlist.size() != stored_makerlist.size()) {
                    return false;
                }

                if(!std::equal(makerlist.begin(), makerlist.end(), stored_makerlist.begin())) {
                    return false;
                }
            }

            if(e.WATCH_TYPE & AOI_WATCH_TYPES::MAKER) {
                std::vector<KEY_TYPE> watcherlist;
                GetWatchersRelatedToPos(e.POS, watcherlist, &key, 1);

                std::vector<KEY_TYPE> stored_watcherlist(e.RELATED_WATCHERS.begin(), e.RELATED_WATCHERS.end());

                std::sort(watcherlist.begin(), watcherlist.end());
                std::sort(stored_watcherlist.begin(), stored_watcherlist.end());

                if(watcherlist.size() != stored_watcherlist.size()) {
                    return false;
                }

                if(!std::equal(watcherlist.begin(), watcherlist.end(), stored_watcherlist.begin())) {
                    return false;
                }
            }
        }

        return true;
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

    void UpdateWatcher(const KEY_TYPE &key, ElementType &element, const ElementType &old_element, const GetMakersInRangeHint *hint = NULL) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
            POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

            POS_TYPE old_lower = old_element.POS[i] - old_element.WATCH_RANGE[i];
            POS_TYPE old_upper = old_element.POS[i] + old_element.WATCH_RANGE[i];

            m_dimensions[i].WATCHER_LOWER_LIST.Update(key, old_lower, lower);
            m_dimensions[i].WATCHER_UPPER_LIST.Update(key, old_upper, upper);
        }

        std::vector<KEY_TYPE> new_makers;

        GetMakersInRange(element.POS, element.WATCH_RANGE, new_makers, &key, 1, hint); // 排除自己，不观察自己
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

    void UpdateMaker(const KEY_TYPE &key, ElementType &element, const ElementType &old_element, const GetWatchersRelatedToPosHint *hint = NULL) {
        for(int i = 0; i < DIMENSION; ++i) {
            m_dimensions[i].MAKER_LIST.Update(key, old_element.POS[i], element.POS[i]);
        }

        std::vector<KEY_TYPE> new_watchers;

        GetWatchersRelatedToPos(element.POS, new_watchers, &key, 1, hint); // 排除自己，不被自己观察
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

    void CalcMoveWatcherHint(ElementType &element, const ElementType &old_element, MoveWatcherHint &hint) {
        static_assert(DIMENSION > 0);

        // 不同维度各自计算，最后相加
        hint.COMPLEXITY = 0;

        for(int d = 0; d < DIMENSION; ++d) {
            
            // LEAVE
            int leave_dimension = -1;
            unsigned long leave_complixity = 0;
            for(int i = 0; i < DIMENSION; ++i) {
                if(i == d) {
                    if(old_element.POS[i] < element.POS[i]) {
                        POS_TYPE old_edge = old_element.POS[i] - old_element.WATCH_RANGE[i];
                        POS_TYPE new_edge = element.POS[i] - element.WATCH_RANGE[i];

                        // old_edge should <= new_edge

                        unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(old_edge, false, new_edge, true);

                        if(leave_dimension < 0 || count < leave_complixity) {
                            leave_dimension = i;
                            leave_complixity = count;
                        }

                    } else {
                        POS_TYPE old_edge = old_element.POS[i] + old_element.WATCH_RANGE[i];
                        POS_TYPE new_edge = element.POS[i] + element.WATCH_RANGE[i];

                        // new_edge should <= old_edge
                        unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(new_edge, true, old_edge, false);

                        if(leave_dimension < 0 || count < leave_complixity) {
                            leave_dimension = i;
                            leave_complixity = count;
                        }
                    }
                } else {
                    POS_TYPE lower = old_element.POS[i] - old_element.WATCH_RANGE[i];
                    POS_TYPE upper = old_element.POS[i] + old_element.WATCH_RANGE[i];
                    unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(lower, false, upper, false);

                    if(leave_dimension < 0 || count < leave_complixity) {
                        leave_dimension = i;
                        leave_complixity = count;
                    }
                }
            }

            hint.LEAVE_DIMENSION[d] = leave_dimension;
            hint.COMPLEXITY += leave_complixity;
            
            // ENTER
            int enter_dimension = -1;
            unsigned long enter_complexity = 0;
            for(int i = 0; i < DIMENSION; ++i) {
                if(i == d) {
                    if(old_element.POS[i] < element.POS[i]) {
                        POS_TYPE old_edge = old_element.POS[i] + old_element.WATCH_RANGE[i];
                        POS_TYPE new_edge = element.POS[i] + element.WATCH_RANGE[i];

                        unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(old_edge, true, new_edge, false);

                        if(enter_dimension < 0 || count < enter_complexity) {
                            enter_dimension = i;
                            enter_complexity = count;
                        }
                    } else {
                        POS_TYPE old_edge = old_element.POS[i] - old_element.WATCH_RANGE[i];
                        POS_TYPE new_edge = element.POS[i] - element.WATCH_RANGE[i];

                        unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(new_edge, false, old_edge, true);

                        if(enter_dimension < 0 || count < enter_complexity) {
                            enter_dimension = i;
                            enter_complexity = count;
                        }
                    }
                } else {
                    POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
                    POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

                    unsigned long count = m_dimensions[i].MAKER_LIST.GetElementsCountByRangedValue(lower, false, upper, false);

                    if(enter_dimension < 0 || count < enter_complexity) {
                        enter_dimension = i;
                        enter_complexity = count;
                    }
                }
            }

            hint.ENTER_DIMENSION[d] = enter_dimension;
            hint.COMPLEXITY += enter_complexity;
        }

    }

    void MoveWatcher(const KEY_TYPE &key, ElementType &element, const ElementType &old_element) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE diff = element.POS[i] < old_element.POS[i] ? old_element.POS[i] - element.POS[i] :
                element.POS[i] - old_element.POS[i];

            if(!(diff < element.WATCH_RANGE[i] + old_element.WATCH_RANGE[i])) {
                UpdateWatcher(key, element, old_element);
                return;
            }
        }

        GetMakersInRangeHint update_hint;
        CalcGetMakersInRangeHint(element.POS, element.WATCH_RANGE, update_hint);

        MoveWatcherHint move_hint;
        CalcMoveWatcherHint(element, old_element, move_hint);

        if(update_hint.COMPLEXITY < move_hint.COMPLEXITY) {
            UpdateWatcher(key, element, old_element, &update_hint);
        } else {
            ShiftWatcher(key, element, old_element, &move_hint);
        }
    }

    void ShiftWatcher(const KEY_TYPE &key, ElementType &element, const ElementType &old_element, MoveWatcherHint *hint) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
            POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

            POS_TYPE old_lower = old_element.POS[i] - old_element.WATCH_RANGE[i];
            POS_TYPE old_upper = old_element.POS[i] + old_element.WATCH_RANGE[i];

            m_dimensions[i].WATCHER_LOWER_LIST.Update(key, old_lower, lower);
            m_dimensions[i].WATCHER_UPPER_LIST.Update(key, old_upper, upper);
        }

        std::vector<KEY_TYPE> leave_makers;
        std::vector<KEY_TYPE> keep_makers;
        std::vector<KEY_TYPE> enter_makers;

        for(int i = 0; i < DIMENSION; ++i) {
            // LEAVE
            int leave_dimension = hint->LEAVE_DIMENSION[i];
            assert(leave_dimension >= 0);

            int d = i;
            auto leave_cb = [&leave_makers, this, key, &old_element, &element, d](unsigned long _0, const KEY_TYPE &k, const POS_TYPE &_1) {
                if(k == key) {
                    return;
                }

                auto iter = this->m_elements.find(k);

                if(iter == this->m_elements.end()) {
                    return;
                }

                ElementType &e = iter->second;

                for(int i = 0; i < DIMENSION; ++i) {
                    if(i == d) {
                        if(old_element.POS[i] < element.POS[i]) {
                            POS_TYPE old_edge = old_element.POS[i] - old_element.WATCH_RANGE[i];
                            POS_TYPE new_edge = element.POS[i] - element.WATCH_RANGE[i];

                            if(!(old_edge < e.POS[i]) || new_edge < e.POS[i]) {
                                return;
                            }
                        } else {
                            POS_TYPE old_edge = old_element.POS[i] + old_element.WATCH_RANGE[i];
                            POS_TYPE new_edge = element.POS[i] + element.WATCH_RANGE[i];

                            if(e.POS[i] < new_edge || !(e.POS[i] < old_edge)) {
                                return;
                            }
                        }
                    } else {
                        POS_TYPE lower = old_element.POS[i] - old_element.WATCH_RANGE[i];
                        POS_TYPE upper = old_element.POS[i] + old_element.WATCH_RANGE[i];

                        if(!(lower < e.POS[i]) || !(e.POS[i] < upper)) {
                            return;
                        }
                    }
                }

                leave_makers.emplace_back(k);
            };

            if(leave_dimension == i) {
                if(old_element.POS[i] < element.POS[i]) {
                    POS_TYPE old_edge = old_element.POS[i] - old_element.WATCH_RANGE[i];
                    POS_TYPE new_edge = element.POS[i] - element.WATCH_RANGE[i];

                    m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(old_edge, false, new_edge, true, leave_cb);
                } else {
                    POS_TYPE old_edge = old_element.POS[i] + old_element.WATCH_RANGE[i];
                    POS_TYPE new_edge = element.POS[i] + element.WATCH_RANGE[i];

                    m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(new_edge, true, old_edge, false, leave_cb);
                }
            } else {
                POS_TYPE lower = old_element.POS[i] - old_element.WATCH_RANGE[i];
                POS_TYPE upper = old_element.POS[i] + old_element.WATCH_RANGE[i];

                m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(lower, false, upper, false, leave_cb);
            }

            // ENTER
            int enter_dimension = hint->ENTER_DIMENSION[i];
            assert(enter_dimension >= 0);

            auto enter_cb = [&enter_makers, this, key, &old_element, &element, d](unsigned long _0, const KEY_TYPE &k, const POS_TYPE &_1) {
                if(k == key) {
                    return;
                }

                auto iter = this->m_elements.find(k);

                if(iter == this->m_elements.end()) {
                    return;
                }

                ElementType &e = iter->second;

                for(int i = 0; i < DIMENSION; ++i) {
                    if(i == d) {
                        if(old_element.POS[i] < element.POS[i]) {
                            POS_TYPE old_edge = old_element.POS[i] + old_element.WATCH_RANGE[i];
                            POS_TYPE new_edge = element.POS[i] + element.WATCH_RANGE[i];

                            if(e.POS[i] < old_edge || !(e.POS[i] < new_edge)) {
                                return;
                            }
                        } else {
                            POS_TYPE old_edge = old_element.POS[i] - old_element.WATCH_RANGE[i];
                            POS_TYPE new_edge = element.POS[i] - element.WATCH_RANGE[i];

                            if(!(new_edge < e.POS[i]) || old_edge < e.POS[i]) {
                                return;
                            }
                        }
                    } else {
                        POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
                        POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

                        if(!(lower < e.POS[i]) || !(e.POS[i] < upper)) {
                            return;
                        }
                    }
                }

                enter_makers.emplace_back(k);
            };

            if(enter_dimension == i) {
                if(old_element.POS[i] < element.POS[i]) {
                        POS_TYPE old_edge = old_element.POS[i] + old_element.WATCH_RANGE[i];
                        POS_TYPE new_edge = element.POS[i] + element.WATCH_RANGE[i];

                        m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(old_edge, true, new_edge, false, enter_cb);
                    } else {
                        POS_TYPE old_edge = old_element.POS[i] - old_element.WATCH_RANGE[i];
                        POS_TYPE new_edge = element.POS[i] - element.WATCH_RANGE[i];

                        m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(new_edge, false, old_edge, true, enter_cb);
                    }
            } else {
                POS_TYPE lower = element.POS[i] - element.WATCH_RANGE[i];
                POS_TYPE upper = element.POS[i] + element.WATCH_RANGE[i];

                m_dimensions[i].MAKER_LIST.GetElementsByRangedValue(lower, false, upper, false, enter_cb);
            }
        }

        std::sort(leave_makers.begin(), leave_makers.end());
        std::sort(enter_makers.begin(), enter_makers.end());

        auto leave_makers_end = std::unique(leave_makers.begin(), leave_makers.end());
        auto enter_makers_end = std::unique(enter_makers.begin(), enter_makers.end());

        leave_makers.resize(leave_makers_end - leave_makers.begin());
        enter_makers.resize(enter_makers_end - enter_makers.begin());

        // 已经得到进入、离开的makers

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
                auto iter = m_elements.find(maker);

                if(iter == m_elements.end()) {
                    continue;
                }

                ElementType &maker_element = iter->second;
                CopyPos(maker_element.POS, event.POS);
                Callback(key, maker, event);
            }
        }
    }

    void CalcMoveMakerHint(ElementType &element, const ElementType &old_element, MoveMakerHint &hint) {
        static_assert(DIMENSION > 0);

        hint.COMPLEXITY = 0;

        for(int d = 0; d < DIMENSION; ++d) {
            // leave
            int leave_dimension = -1;
            bool leave_use_lower = true;
            unsigned long leave_complixity = 0;
            for(int i = 0; i < DIMENSION; ++i) {
                if(i == d) {
                    if(old_element.POS[i] < element.POS[i]) {
                        unsigned long count = m_dimensions[i].WATCHER_UPPER_LIST.GetElementsCountByRangedValue(old_element.POS[i], false, element.POS[i], true);

                        if(leave_dimension < 0 || count < leave_complixity) {
                            leave_dimension = i;
                            leave_complixity = count;
                        }
                    } else {
                        unsigned long count = m_dimensions[i].WATCHER_LOWER_LIST.GetElementsCountByRangedValue(element.POS[i], true, old_element.POS[i], false);

                        if(leave_dimension < 0 || count < leave_complixity) {
                            leave_dimension = i;
                            leave_complixity = count;
                        }
                    }
                } else {
                    POS_TYPE lower_begin = old_element.POS[i] - m_max_watch_range[i];
                    POS_TYPE lower_end = old_element.POS[i];

                    unsigned long count = m_dimensions[i].WATCHER_LOWER_LIST.GetElementsCountByRangedValue(lower_begin, false, lower_end, false);

                    if(leave_dimension < 0 || count < leave_complixity) {
                        leave_dimension = i;
                        leave_use_lower = true;
                        leave_complixity = count;
                    }

                    POS_TYPE upper_begin = old_element.POS[i];
                    POS_TYPE upper_end = old_element.POS[i] + m_max_watch_range[i];

                    count = m_dimensions[i].WATCHER_UPPER_LIST.GetElementsCountByRangedValue(upper_begin, false, upper_end, false);

                    if(count < leave_complixity) {
                        leave_dimension = i;
                        leave_use_lower = false;
                        leave_complixity = count;
                    }
                }
            }

            hint.LEAVE_DIMENSION[d] = leave_dimension;
            hint.LEAVE_USE_LOWER[d] = leave_use_lower;
            hint.COMPLEXITY += leave_complixity;

            // enter
            int enter_dimension = -1;
            bool enter_use_lower = true;
            unsigned long enter_complexity = 0;
            for(int i = 0; i < DIMENSION; ++i) {
                if(i == d) {
                    if(old_element.POS[i] < element.POS[i]) {
                        unsigned long count = m_dimensions[i].WATCHER_LOWER_LIST.GetElementsCountByRangedValue(old_element.POS[i], true, element.POS[i], false);

                        if(enter_dimension < 0 || count < enter_complexity) {
                            enter_dimension = i;
                            enter_complexity = count;
                        }
                    } else {
                        unsigned long count = m_dimensions[i].WATCHER_UPPER_LIST.GetElementsCountByRangedValue(element.POS[i], false, old_element.POS[i], true);

                        if(enter_dimension < 0 || count < enter_complexity) {
                            enter_dimension = i;
                            enter_complexity = count;
                        }
                    }
                } else {
                    POS_TYPE lower_begin = element.POS[i] - m_max_watch_range[i];
                    POS_TYPE lower_end = element.POS[i];

                    unsigned long count = m_dimensions[i].WATCHER_LOWER_LIST.GetElementsCountByRangedValue(lower_begin, false, lower_end, false);

                    if(enter_dimension < 0 || count < enter_complexity) {
                        enter_dimension = i;
                        enter_use_lower = true;
                        enter_complexity = count;
                    }

                    POS_TYPE upper_begin = element.POS[i];
                    POS_TYPE upper_end = element.POS[i] + m_max_watch_range[i];

                    count = m_dimensions[i].WATCHER_UPPER_LIST.GetElementsCountByRangedValue(upper_begin, false, upper_end, false);

                    if(count < enter_complexity) {
                        enter_dimension = i;
                        enter_use_lower = false;
                        enter_complexity = count;
                    }
                }
            }

            hint.ENTER_DIMENSION[d] = enter_dimension;
            hint.ENTER_USE_LOWER[d] = enter_use_lower;
            hint.COMPLEXITY += enter_complexity;
        }
    }

    void MoveMaker(const KEY_TYPE &key, ElementType &element, const ElementType &old_element) {
        for(int i = 0; i < DIMENSION; ++i) {
            POS_TYPE diff = element.POS[i] < old_element.POS[i] ? old_element.POS[i] - element.POS[i] :
            element.POS[i] - old_element.POS[i];

            if(!(diff < m_max_watch_range[i] + m_max_watch_range[i])) {
                UpdateMaker(key, element, old_element);
                return;
            }
        }

        GetWatchersRelatedToPosHint update_hint;
        CalcGetWatchersRelatedToPosHint(element.POS, update_hint);

        MoveMakerHint move_hint;
        CalcMoveMakerHint(element, old_element, move_hint);

        if(update_hint.COMPLEXITY < move_hint.COMPLEXITY) {
            UpdateMaker(key, element, old_element, &update_hint);
        } else {
            ShiftMaker(key, element, old_element, &move_hint);
        }
    }

    void ShiftMaker(const KEY_TYPE &key, ElementType &element, const ElementType &old_element, MoveMakerHint *hint) {
        for(int i = 0; i < DIMENSION; ++i) {
            m_dimensions[i].MAKER_LIST.Update(key, old_element.POS[i], element.POS[i]);
        }

        std::vector<KEY_TYPE> leave_watchers;
        std::vector<KEY_TYPE> keep_watchers;
        std::vector<KEY_TYPE> enter_watchers;

        for(int i = 0; i < DIMENSION; ++i) {
            // leave
            int leave_dimension = hint->LEAVE_DIMENSION[i];
            bool leave_use_lower = hint->LEAVE_USE_LOWER[i];
            assert(leave_dimension >= 0);

            int d = i;
            auto leave_cb = [&leave_watchers, this, key, &old_element, &element, d](unsigned long _0, const KEY_TYPE &k, const POS_TYPE &_1) {
                // filter watchers who can SEE old_element
                
                if(k == key) {
                    return;
                }

                auto iter = this->m_elements.find(k);

                if(iter == this->m_elements.end()) {
                    return;
                }

                ElementType &e = iter->second;
                
                for(int i = 0; i < DIMENSION; ++i) {
                    POS_TYPE lower = e.POS[i] - e.WATCH_RANGE[i];
                    POS_TYPE upper = e.POS[i] + e.WATCH_RANGE[i];

                    if(i == d) {
                        if(old_element.POS[i] < element.POS[i]) {
                            if(!( lower < old_element.POS[i] && old_element.POS[i] < upper && !(element.POS[i] < upper))) {
                                return;
                            }
                        } else {
                            if(!( !(lower < element.POS[i]) && lower < old_element.POS[i] && old_element.POS[i] < upper )) {
                                return;
                            }
                        }
                    } else {
                        if( !(lower < old_element.POS[i]) || !(old_element.POS[i] < upper) ) {
                            return;
                        }
                    }
                }

                leave_watchers.emplace_back(k);
            };

            if(i == leave_dimension) {
                if(old_element.POS[i] < element.POS[i]) {
                    m_dimensions[i].WATCHER_UPPER_LIST.GetElementsByRangedValue(old_element.POS[i], false, element.POS[i], true, leave_cb);
                } else {
                    m_dimensions[i].WATCHER_LOWER_LIST.GetElementsByRangedValue(element.POS[i], true, old_element.POS[i], false, leave_cb);
                }
            } else {
                if(leave_use_lower) {
                    POS_TYPE lower_begin = old_element.POS[i] - m_max_watch_range[i];
                    POS_TYPE lower_end = old_element.POS[i];

                    m_dimensions[i].WATCHER_LOWER_LIST.GetElementsByRangedValue(lower_begin, false, lower_end, false, leave_cb);
                } else {
                    POS_TYPE upper_begin = old_element.POS[i];
                    POS_TYPE upper_end = old_element.POS[i] + m_max_watch_range[i];

                    m_dimensions[i].WATCHER_UPPER_LIST.GetElementsByRangedValue(upper_begin, false, upper_end, false, leave_cb);
                }
            }


            // enter
            int enter_dimension = hint->ENTER_DIMENSION[i];
            bool enter_use_lower = hint->ENTER_USE_LOWER[i];
            assert(enter_dimension >= 0);

            auto enter_cb = [&enter_watchers, this, key, &old_element, &element, d](unsigned long _0, const KEY_TYPE &k, const POS_TYPE &_1) {
                // filter watchers who can SEE element

                if(k == key) {
                    return;
                }

                auto iter = this->m_elements.find(k);

                if(iter == this->m_elements.end()) {
                    return;
                }

                ElementType &e = iter->second;
                
                for(int i = 0; i < DIMENSION; ++i) {
                    POS_TYPE lower = e.POS[i] - e.WATCH_RANGE[i];
                    POS_TYPE upper = e.POS[i] + e.WATCH_RANGE[i];

                    if(i == d) {
                        if(old_element.POS[i] < element.POS[i]) {
                            if(!( !(lower < old_element.POS[i]) && lower < element.POS[i] && element.POS[i] < upper )) {
                                return;
                            }
                        } else {
                            if(!( lower < element.POS[i] && element.POS[i] < upper && !(old_element.POS[i] < upper) )) {
                                return;
                            }
                        }
                    } else {
                        if( !(lower < element.POS[i]) || !(element.POS[i] < upper) ) {
                            return;
                        }
                    }
                }

                enter_watchers.emplace_back(k);
            };

            if(i == enter_dimension) {
                
            } else {
                if(enter_use_lower) {
                    POS_TYPE lower_begin = element.POS[i] - m_max_watch_range[i];
                    POS_TYPE lower_end = element.POS[i];

                    m_dimensions[i].WATCHER_LOWER_LIST.GetElementsByRangedValue(lower_begin, false, lower_end, false, enter_cb);
                } else {
                    POS_TYPE upper_begin = element.POS[i];
                    POS_TYPE upper_end = element.POS[i] + m_max_watch_range[i];

                    m_dimensions[i].WATCHER_UPPER_LIST.GetElementsByRangedValue(upper_begin, false, upper_end, false, enter_cb);
                }
            }
        }

        std::sort(leave_watchers.begin(), leave_watchers.end());
        std::sort(enter_watchers.begin(), enter_watchers.end());

        auto leave_watchers_end = std::unique(leave_watchers.begin(), leave_watchers.end());
        auto enter_watchers_end = std::unique(enter_watchers.begin(), enter_watchers.end());

        leave_watchers.resize(leave_watchers_end - leave_watchers.begin());
        enter_watchers.resize(enter_watchers_end - enter_watchers.begin());

        for(const KEY_TYPE &watcher: leave_watchers) {
            element.RELATED_WATCHERS.erase(watcher);

            auto iter = m_elements.find(watcher);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &watcher_element = iter->second;
            watcher_element.RELATED_MAKERS.erase(key);
        }

        keep_watchers.assign(element.RELATED_WATCHERS.begin(), element.RELATED_WATCHERS.end());

        for(const KEY_TYPE &watcher: enter_watchers) {
            element.RELATED_WATCHERS.insert(watcher);

            auto iter = m_elements.find(watcher);

            if(iter == m_elements.end()) {
                continue;
            }

            ElementType &watcher_element = iter->second;
            watcher_element.RELATED_MAKERS.insert(key);
        }

        // notify
        if(leave_watchers.size() || keep_watchers.size() || enter_watchers.size()) {
            AOI_EVENT_TYPE event;
            
            CopyPos(element.POS, event.POS);
            CopyPos(old_element.POS, event.POS_FROM);

            event.EVENT_ID = AOI_EVENT_IDS::LEAVE;
            for(const KEY_TYPE &watcher: leave_watchers) {
                Callback(watcher, key, event);
            }

            event.EVENT_ID = AOI_EVENT_IDS::MOVE;
            for(const KEY_TYPE &watcher: keep_watchers) {
                Callback(watcher, key, event);
            }

            event.EVENT_ID = AOI_EVENT_IDS::ENTER;
            for(const KEY_TYPE &watcher: enter_watchers) {
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
