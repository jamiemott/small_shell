# Program Name:	Small Shell
# Author:		Jamie Mott
# Date :		February 25, 2020


# Compiler
CXX = gcc

# Compiler flags
#CXXFLAGS = -Wall


smshell: smshell.c
	$(CXX) -o smshell smshell.c


clean:
	rm smshell