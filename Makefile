TARGET = fat32 
CCX    = g++
FLAGS  = -std=c++20
SRC    = src
BIN    = bin
SOURCE = $(wildcard $(SRC)/*.cpp) 
OBJECT = $(patsubst %,$(BIN)/%, $(notdir $(SOURCE:.cpp=.o)))

$(TARGET) : $(OBJECT)
	$(CCX) $(FLAGS) -o $@ $^

$(BIN)/%.o : $(SRC)/%.cpp
	@mkdir -p $(BIN)
	$(CCX) $(FLAGS) -c $< -o $@

.PHONY clean:
clean:
	rm -rf $(BIN) $(TARGET)