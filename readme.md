
OBINexusComputing@nnamdiHP MINGW64 ~/projects/pkg
$ cd libpolycall/libpolycall/

OBINexusComputing@nnamdiHP MINGW64 ~/projects/pkg/libpolycall/libpolycall
$ make clean
rm -rf build lib bin

OBINexusComputing@nnamdiHP MINGW64 ~/projects/pkg/libpolycall/libpolycall
$ make
gcc -Wall -Wextra -I./include -fPIC -D_WIN32 -MMD -MP -c src/network.c -o build/network.o
gcc -Wall -Wextra -I./include -fPIC -D_WIN32 -MMD -MP -c src/polycall.c -o build/polycall.o
gcc -Wall -Wextra -I./include -fPIC -D_WIN32 -MMD -MP -c src/polycall_protocol.c -o build/polycall_protocol.o
gcc -Wall -Wextra -I./include -fPIC -D_WIN32 -MMD -MP -c src/polycall_state_machine.c -o build/polycall_state_machine.o
ar rcs lib/libpolycall.a build/network.o build/polycall.o build/polycall_protocol.o build/polycall_state_machine.o
gcc -shared -o lib/libpolycall.dll build/network.o build/polycall.o build/polycall_protocol.o build/polycall_state_machine.o -pthread -lssl -lcrypto -lws2_32
gcc -Wall -Wextra -I./include -fPIC -D_WIN32 -MMD -MP -c main.c -o build/main.o
main.c: In function 'main':
main.c:296:35: warning: variable 'arg3' set but not used [-Wunused-but-set-variable]
  296 |     char *command, *arg1, *arg2, *arg3;
      |                                   ^~~~
main.c:296:28: warning: variable 'arg2' set but not used [-Wunused-but-set-variable]
  296 |     char *command, *arg1, *arg2, *arg3;
      |                            ^~~~
gcc build/main.o lib/libpolycall.a -o bin/polycall.exe -pthread -lssl -lcrypto -lws2_32 -Llib -l:libpolycall.a

OBINexusComputing@nnamdiHP MINGW64 ~/projects/pkg/libpolycall/libpolycall
$ ./b
bin/   build/

OBINexusComputing@nnamdiHP MINGW64 ~/projects/pkg/libpolycall/libpolycall
$ ./bin/polycall.exe
PolyCall CLI v1.0.0 - Type 'help' for commands

> help

PolyCall CLI Commands:
Network Commands:
  start_network          - Start network services
  stop_network           - Stop network services
  list_endpoints         - List all network endpoints
  list_clients          - List connected clients

State Machine Commands:
  init                  - Initialize the state machine
  add_state NAME        - Add a new state
  add_transition NAME FROM TO - Add a transition
  execute NAME          - Execute a transition
  lock STATE_ID         - Lock a state
  unlock STATE_ID       - Unlock a state
  verify STATE_ID       - Verify state integrity
  snapshot STATE_ID     - Create state snapshot
  restore STATE_ID      - Restore from snapshot
  diagnostics STATE_ID  - Get state diagnostics

Miscellaneous Commands:
  list_states          - List all states
  list_transitions     - List all transitions
  history              - Show command history
  status              - Show system status
  help                - Show this help message
  quit                - Exit the program

> start_network
Unknown command. Type 'help' for available commands

> start_network
Using port 8080
Network program initialized successfully on port 8080
Network services started

>
