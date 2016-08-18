VERSION		:= "\"1.50.3m\""

BUILD		:= build
SOURCES		:= source

CFLAGS		:= -DPACKAGE_VERSION=$(VERSION)
CXXFLAGS	:= $(CFLAGS)
LDFLAGS		:=

LIBS		:=

CFILES		:= $(foreach dir, $(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir, $(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

OFILES		:= 	$(addprefix $(BUILD)/, $(addsuffix .o, $(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) )

ndstool: $(OFILES)
	g++ $(LDFLAGS) -o $@ $^

clean:
	rm -r $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.cpp
	mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(SOURCES)/%.c
	mkdir -p $(BUILD)
	gcc $(CFLAGS) -c -o $@ $<

-include $(OBJFILES:.o=.d)
