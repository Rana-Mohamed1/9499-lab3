## you have command.cc command.h tokenizer.cc tokenizer.h you need compile them and link
## them to one object file called  : "myshell" 
## so we should run make , that will create our object myshell ,
## ./myshell should run your application 
CC = g++ -g
all: myshell
myshell: command.cc command.h tokenizer.cc tokenizer.h
	$(CC) -o myshell command.cc tokenizer.cc
clean:
	rm -f myshell *.o	