#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"
#include <errno.h>

static int logFileDescriptor = -1;
static bool joinQuote (std::vector<Token> &tokens, size_t &i, std::string &word)
{
    word = tokens[i].value;
    char quote = word[0];
    if (quote != '\'' && quote != '"')
        return true;
    while (word.size() < 2 || word[word.size() - 1] != quote)
    {
        if (tokens[i +1 ].type == TOKEN_EOF)
            return false;
        i++;
        word += " " + tokens[i].value;
    }
    word = word.substr(1, word.size() - 2);
    return true;
}

void parse(std::vector<Token> &tokens)
{
    if (tokens.empty() || tokens[0].type == TOKEN_EOF)
        return;
    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();
    TokenType redirectionType = TOKEN_EOF;
    const char *error = NULL;
    const char *missingFileError = "Missing file name after redirection\n";
    size_t i = 0; 
    while (i < tokens.size() && tokens[i].type != TOKEN_EOF && !error)
    {
        Token token = tokens[i];
        if (token.type == TOKEN_COMMAND || token.type == TOKEN_ARGUMENT)
        {
            std::string word;
            if (!joinQuote(tokens, i, word))
                error = "Unmatched quote in command\n";
            else if (redirectionType == TOKEN_EOF)  
                currentSimpleCommand->insertArgument(strdup(word.c_str()));  
            else  
            {
                if (redirectionType == TOKEN_INPUT)
                {
                    if (currentCommand._inputFile)
                        free(currentCommand._inputFile); 
                    currentCommand._inputFile = strdup(word.c_str());
                }
                else if (redirectionType == TOKEN_ERROR)
                {
                    if (currentCommand._errFile)
                        free(currentCommand._errFile);
                    currentCommand._errFile = strdup(word.c_str());
                }
                else if (redirectionType == TOKEN_REDIRECT || redirectionType == TOKEN_APPEND)
                {
                    if (currentCommand._outFile)
                        free(currentCommand._outFile);
                    currentCommand._outFile = strdup(word.c_str());
                    currentCommand._append = (redirectionType == TOKEN_APPEND) ;
                }
                else if (redirectionType == TOKEN_REDIRECT_AND_ERROR)
                {
                    if (currentCommand._errFile)
                        free(currentCommand._errFile);
                    currentCommand._errFile = strdup(word.c_str());
                    currentCommand._out_error = 1;
                    currentCommand._append = 1;
                }
                redirectionType = TOKEN_EOF;
            }
        }
        else if (token.type == TOKEN_PIPE)
        {
            if (redirectionType != TOKEN_EOF )
                error = missingFileError;
            else if (currentSimpleCommand->_numberOfArguments == 0)
                error = "Missing command before | (pipe)\n";
            else
            {
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentSimpleCommand = new SimpleCommand();
            }    
        }
        else if (token.type == TOKEN_BACKGROUND)
        {
            if (i + 1 >= tokens.size() || tokens[i + 1].type != TOKEN_EOF)
                error = "Background operator '&' must be at the end of the command\n";
            else     
                currentCommand._background = 1;
        }
        else 
        {
            if (redirectionType != TOKEN_EOF)
                error = missingFileError;
            else
                redirectionType = token.type;
        }
        i++;
    }
    if (!error && redirectionType != TOKEN_EOF)
        error = missingFileError;
    if (!error && currentSimpleCommand->_numberOfArguments == 0)
        error = currentCommand._numberOfSimpleCommands > 0 ? "Missing command after | (pipe)\n" : "Missing command\n" ;
    currentCommand.insertSimpleCommand(currentSimpleCommand);
    if (error)
        fprintf(stderr, "%s", error);
    else     
        currentCommand.execute();
    currentCommand.clear();
    free(currentCommand._simpleCommands);    
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete(_simpleCommands[i]);
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}
static void closeAll(const std::vector<int> &fds){
    for (size_t i =0 ; i <fds.size();i++)
        close(fds[i]);
}
static void redirectTo (const char * file ,int flags,int target){
    int fd=open (file ,flags,0644);
    if (fd==-1){
        perror(file);
        _exit(1);
    }
    dup2(fd,target);
    close (fd);
}
static void logChildTermination(pid_t pid, int status)
{
    if (logFileDescriptor == -1)
        return;
    char when[32];
    time_t now = time(NULL);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
    if (WIFEXITED(status))
        dprintf(logFileDescriptor, "[%s] Child process %d terminated with exit status %d\n", when, (int) pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        dprintf(logFileDescriptor, "[%s] Child process %d terminated by signal %d\n", when, (int) pid, WTERMSIG(status));
    else 
        dprintf(logFileDescriptor, "[%s] Child process %d terminated\n", when, (int) pid);
}
void childHandler(int signalNumber)
{
    (void)signalNumber;
    int saved_errno = errno;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        logChildTermination(pid, status);
    errno = saved_errno;
}
void Command::execute()
{
    int n = _numberOfSimpleCommands;
    if (n == 0)
        return;
    print();
    if (n == 1 && strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0)
    {
        printf("Good bye!!\n");
        clear();
        exit(0);
    }
    if(n == 1 && strcmp(_simpleCommands[0]->_arguments[0], "cd")==0)
    {
        char *path = _simpleCommands[0]->_arguments[1];
        if(path == NULL)
            path = getenv("HOME"); 
        if (path == NULL || chdir(path) == -1)
            perror("cd");
        return;            
    }
    std::vector<std::vector<std::string> > arguments(n);
    for (int i = 0; i < n; i++)
    {
        SimpleCommand *simpleCommand = _simpleCommands[i];
        for (int j = 0; j < simpleCommand->_numberOfArguments; j++)
        {
            char *arg = simpleCommand->_arguments[j];
            glob_t glob_result;
            memset(&glob_result, 0, sizeof(glob_result));
            if ((strchr(arg, '*')|| strchr(arg, '?'))&& glob (arg,0, NULL, &glob_result)==0)
            {
                for (size_t k = 0; k < glob_result.gl_pathc; k++)
                    arguments[i].push_back(glob_result.gl_pathv[k]);
            }
            else
            {
                arguments[i].push_back(arg);
            }
                globfree(&glob_result);
        }    
    } 
    std::vector<int> fds(2*(n-1));
    for (int k =0;k < n-1 ; k++){
        if (pipe(&fds[2*k])==-1){
            perror("pipe");
            return;
        }
    }
    fflush(stdout);
    std::vector<pid_t> pids;
    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork");
            break;
        }
        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);
            if (i>0)
                dup2(fds[2*(i-1)],STDIN_FILENO);    
            if (i < n - 1)
                dup2(fds[2*i+1],STDOUT_FILENO);
            closeAll(fds);    
            if (_inputFile && i == 0)
                redirectTo(_inputFile,O_RDONLY,STDIN_FILENO);
            if (i == n - 1)
            {
                if (_outFile)
                    redirectTo(_outFile,O_WRONLY | O_CREAT | (_append ? O_APPEND: O_TRUNC),STDOUT_FILENO);
                if (_errFile)
                {
                    redirectTo(_errFile,O_WRONLY | O_CREAT |(_out_error ? O_APPEND: O_TRUNC),STDERR_FILENO );
                    if (_out_error)
                        dup2(STDERR_FILENO,STDOUT_FILENO);
                }
            }
            std::vector<char *> argv;
            for (size_t j = 0; j < arguments[i].size(); j++)
                argv.push_back(const_cast<char *>(arguments[i][j].c_str()));
            argv.push_back(NULL);
            execvp(argv[0], argv.data());
            perror(argv[0]);
            _exit(1); 
        }   
        pids.push_back(pid);
    } 
    closeAll(fds);
    if (! _background)
    {
        for (size_t i = 0; i < pids.size(); i++)
        {
            int status;
            if (waitpid(pids[i],&status,0)>0)
                logChildTermination(pids[i],status);
        }
    }  
}

void Command::prompt()
{
    std::string input;
    while (true)
    {
        printf("myshell> ");
        fflush(stdout);
        if (!std::getline(std::cin, input))
        {
            break; 
        }
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}
Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    signal(SIGINT, SIG_IGN);
    logFileDescriptor=open("myshell.log",O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,0644);
    if (logFileDescriptor == -1)
        perror("myshell.log");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = childHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        return 1;
    }
    Command::_currentCommand.prompt();
    return 0;
}
