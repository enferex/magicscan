APP = magicscan
CXXFLAGS = -pedantic -std=c++17 -Wall -lmagic -pthread

all: release

release: CXXFLAGS += -O3 -g0
release: $(APP)

debug: CXXFLAGS += -O0 -g3 -DDEBUG
debug: $(APP)

$(APP): main.cc
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	$(RM) $(APP)
