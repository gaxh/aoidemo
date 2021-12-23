#include "aoi_group.h"
#include <iostream>
#include <time.h>
#include <random>
#include <unordered_map>

constexpr int OP_EXIT = 0;
constexpr int OP_ENTER = 1;
constexpr int OP_LEAVE = 2;
constexpr int OP_MOVE = 3;
constexpr int OP_WATCHTYPE = 4;
constexpr int OP_RANGE = 5;
constexpr int OP_DUMP = 6;

const char *OpRepr(int op) {
    switch(op) {
        case OP_EXIT:
            return "EXIT";
        case OP_ENTER:
            return "ENTER";
        case OP_LEAVE:
            return "LEAVE";
        case OP_MOVE:
            return "MOVE";
        case OP_WATCHTYPE:
            return "WATCHTYPE";
        case OP_RANGE:
            return "RANGE";
        case OP_DUMP:
            return "DUMP";
        default:
            return "UNKNOWN";
    }
}

template<typename T>
void InputSingle(T &v, const char *hint) {
    std::cout << hint << "\n";
    std::cin >> v;
    std::cout << "inputed: " << v << "\n";
}

template<typename T, int DIMENSION>
void InputArray(T v[DIMENSION], const char *hint) {
    std::cout << hint << " (" << DIMENSION << "-array" << ")" << "\n";

    for(int i = 0; i < DIMENSION; ++i) {
        std::cin >> v[i];
    }

    std::cout << "inputed: ";
    for(int i = 0; i < DIMENSION; ++i) {
        std::cout << v[i] << " ";
    }
    std::cout << "\n";
}

void TestInteractive() {
    constexpr int DIMENSION = 2;

    long max_watch_range[DIMENSION];

    for(int i = 0; i < DIMENSION; ++i) {
        max_watch_range[i] = 20;
    }

    AoiGroup<unsigned, long, DIMENSION> group(max_watch_range);

    group.SetCallback([](unsigned receiver, unsigned sender, AoiGroup<unsigned, long, DIMENSION>::AOI_EVENT_TYPE event){
                std::cout << "* EVENT=" << AoiEventIdRepr(event.EVENT_ID) << " ";
                std::cout << "RECEIVER=" << receiver << " ";
                std::cout << "SENDER=" << sender << " ";
                std::cout << "POS=(";
                for(int i = 0; i < DIMENSION; ++i) {
                    std::cout << event.POS[i] << ",";
                }
                std::cout << ")";

                if(event.EVENT_ID == AOI_EVENT_IDS::MOVE) {
                    std::cout << " POS_FROM=(";
                    for(int i = 0; i < DIMENSION; ++i) {
                        std::cout << event.POS_FROM[i] << ",";
                    }
                    std::cout << ")";
                }
                std::cout << "\n";
            });

    long pos[DIMENSION];
    long watch_range[DIMENSION];

    int op;

    bool running = true;
    unsigned id;
    int watch_type;

    while(running) {
        InputSingle(op, "enter operation: 1:enter 2:leave 3:move 4:watchtype 5:watchrange 6:dump 0:exit");

        switch(op) {
            case OP_EXIT:
                running = false;
                break;
            case OP_ENTER:
                {
                    InputSingle(id, "enter element id:");
                    InputArray<long, DIMENSION>(pos, "enter pos:");
                    InputSingle(watch_type, "enter watch_type:");
                    if(watch_type & AOI_WATCH_TYPES::WATCHER) {
                        InputArray<long, DIMENSION>(watch_range, "enter watch_range:");
                    }

                    bool result = group.Enter(id, pos, watch_type, watch_range);
                    std::cout << "result=" << result << "\n";
                }
                break;
            case OP_LEAVE:
                {
                    InputSingle(id, "enter element id:");
                    bool result = group.Leave(id);
                    std::cout << "result=" << result << "\n";
                }
                break;
            case OP_MOVE:
                {
                    InputSingle(id, "enter element id:");
                    InputArray<long, DIMENSION>(pos, "enter pos:");
                    bool result = group.Move(id, pos);
                    std::cout << "result=" << result << "\n";
                }
                break;
            case OP_WATCHTYPE:
                {
                    InputSingle(id, "enter element id:");
                    InputSingle(watch_type, "enter watch_type:");
                    bool result = group.ChangeWatchType(id, watch_type);
                    std::cout << "result=" << result << "\n";
                }
                break;
            case OP_RANGE:
                {
                    InputSingle(id, "enter element id:");
                    InputArray<long, DIMENSION>(watch_range, "enter watch_range:");
                    bool result = group.ChangeWatchRange(id, watch_range);
                    std::cout << "result=" << result << "\n";
                }
                break;
            case OP_DUMP:
                {
                    std::cout << group.DumpElements() << "\n";
                    std::cout << group.DumpSlist() << "\n";
                }
                break;
            default:
                std::cout << "UNKNOWN OPERATION: " << op << "\n";
                break;
        }
    }
}

void TestStress() {
    constexpr int DIMENSION = 2;

    long max_watch_range[DIMENSION];
    for(int i = 0; i < DIMENSION; ++i) {
        max_watch_range[i] = 20;
    }

    AoiGroup<unsigned, long, DIMENSION> group(max_watch_range);
    std::mt19937 rng;
    rng.seed(time(NULL));

    constexpr long pos_max = 2000;
    constexpr unsigned id_max = 20000;

    // 插入 id_max 个元素
    {
        std::cout << "begin insert elements: " << id_max << "\n";

        unsigned inserted = 0;
        clock_t tbegin = clock();
        for(unsigned id = 0; id < id_max; ++id) {
            long pos[DIMENSION];
            for(int i = 0; i < DIMENSION; ++i) {
                pos[i] = (long)rng() % pos_max;
            }
            long watch_range[DIMENSION];
            for(int i = 0; i < DIMENSION; ++i) {
                watch_range[i] = ((long)rng() % max_watch_range[i]) + 1;
            }
            inserted += group.Enter(id, pos, 3, watch_range) ? 1 : 0;
        }
        clock_t tdiff = clock() - tbegin;

        std::cout << "finish insert elements: " << inserted << " COST_TIME=" << (double)tdiff / CLOCKS_PER_SEC << "\n";
    }

    // 移动所有元素
    {
        std::cout << "begin move elements: " << id_max << "\n";

        unsigned moved = 0;
        clock_t tbegin = clock();
        for(unsigned id = 0; id < id_max; ++id) {
            long pos[DIMENSION];
            for(int i = 0; i < DIMENSION; ++i) {
                pos[i] = (long)rng() % pos_max;
            }
            moved += group.Move(id, pos) ? 1 : 0;
        }
        clock_t tdiff = clock() - tbegin;

        std::cout << "finish move elements: " << moved << " COST_TIME=" << (double)tdiff / CLOCKS_PER_SEC << "\n";
    }

    // 删除所有元素
    {
        std::cout << "begin remove elements: " << id_max << "\n";

        unsigned deleted = 0;
        clock_t tbegin = clock();
        for(unsigned id = 0; id < id_max; ++id) {
            deleted += group.Leave(id) ? 1 : 0;
        }
        clock_t tdiff = clock() - tbegin;

        std::cout << "finish remove elements: " << deleted << " COST_TIME=" << (double)tdiff / CLOCKS_PER_SEC << "\n";
    }

    std::cout << "DUMP: " << "\n";
    std::cout << group.DumpElements() << "\n";
    std::cout << group.DumpSlist() << "\n";
}

int main() {
    //TestInteractive();
    TestStress();

    return 0;
}



