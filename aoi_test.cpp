#include "aoi_group.h"
#include <iostream>

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

    long max_watcher_range[DIMENSION];

    for(int i = 0; i < DIMENSION; ++i) {
        max_watcher_range[i] = 20;
    }

    AoiGroup<unsigned, long, DIMENSION> group(max_watcher_range);

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

    constexpr int OP_EXIT = 0;
    constexpr int OP_ENTER = 1;
    constexpr int OP_LEAVE = 2;
    constexpr int OP_MOVE = 3;
    constexpr int OP_WATCHTYPE = 4;
    constexpr int OP_RANGE = 5;
    constexpr int OP_DUMP = 6;
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


int main() {
    TestInteractive();


    return 0;
}



