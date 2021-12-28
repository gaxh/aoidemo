all : aoitest

aoitest : 3rd/rankcpp/zeeset.h aoi_group.h aoi_test.cpp
	clang++-11 aoi_test.cpp -o $@ -g -O2 -Wall -I3rd/rankcpp/

clean:
	rm -f aoitest
