#include <cstdio>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>

#include <net/net.h>
#include <net/netdb.h>
#include <netinet/in.h>
#include <sys/thread.h>

#include <NoRSX.h>

#include "const.h"
#include "server.h"
#include "client.h"
#include "command.h"

using namespace std;

void server_start(void* arg)
{
    // Get NoRSX handle to be able to listen to system events.
    NoRSX* gfx;
    gfx = (NoRSX*)arg;

    // Create server variables.
    vector<pollfd> pollfds;
    map<int, Client*> clients;
    map<int, Client*> clients_data;
    map<string, cmdfunc> commands;

    // Register server commands.
    register_cmds(&commands);

    // Create server socket.
    int server;
    server = socket(PF_INET, SOCK_STREAM, 0);

    sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(21);
    myaddr.sin_addr.s_addr = INADDR_ANY;

    // Bind port 21 to server socket.
    if(bind(server, (sockaddr*)&myaddr, sizeof myaddr) != 0)
    {
        // Could not bind port to socket.
        gfx->AppExit();
        closesocket(server);
        sysThreadExit(1);
    }

    // Start listening for connections.
    listen(server, LISTEN_BACKLOG);

    // Create and add server pollfd.
    pollfd server_pollfd;
    server_pollfd.fd = server;
    server_pollfd.events = POLLIN;

    pollfds.push_back(server_pollfd);

    // Server loop.
    while(gfx->GetAppStatus())
    {
        int p;
        p = poll(&pollfds[0], (nfds_t)pollfds.size(), 250);

        if(p == -1)
        {
            // poll error
            gfx->AppExit();
            break;
        }

        if(p == 0)
        {
            // no new events
            continue;
        }

        // new event detected!
        // iterate through connected sockets
        for(vector<pollfd>::iterator pfd_it = pollfds.begin(); pfd_it != pollfds.end(); pfd_it++)
        {
            if(!p) break;

            pollfd pfd;
            pfd = *pfd_it;

            if(pfd.revents != 0)
            {
                p--;

                // handle socket events, depending on socket type
                // server
                if(pfd.fd == server)
                {
                    // incoming connection
                    int client_new = accept(server, NULL, NULL);

                    if(client_new == -1)
                    {
                        continue;
                    }

                    // create and add pollfd
                    pollfd client_pollfd;
                    client_pollfd.fd = client_new;
                    client_pollfd.events = (POLLIN|POLLRDNORM);

                    pollfds.push_back(client_pollfd);

                    // create new internal client
                    Client* client = new Client(client_new, &pollfds, &clients, &clients_data);

                    // assign socket to internal client
                    clients.insert(make_pair(client_new, client));

                    // hello!
                    client->send_multicode(220, "Welcome to OpenPS3FTP!");

                    /*client->send_string(" Supported commands:");

                    for(map<string, cmdfunc>::iterator cmds_it = commands.begin(); cmds_it != commands.end(); cmds_it++)
                    {
                        client->send_string("  " + cmds_it->first);
                    }*/

                    client->send_code(220, "Ready.");
                    continue;
                }
                else
                {
                    // check if socket is data
                    map<int, Client*>::iterator cdata_it;
                    cdata_it = clients_data.find(pfd.fd);

                    if(cdata_it != clients_data.end())
                    {
                        // get client
                        Client* client = cdata_it->second;

                        // check disconnect event
                        if(pfd.revents&(POLLNVAL|POLLHUP|POLLERR))
                        {
                            client->data_end();
                            continue;
                        }

                        // handle data operation
                        if(pfd.revents&(POLLOUT|POLLWRNORM|POLLIN|POLLRDNORM))
                        {
                            client->handle_data();
                            continue;
                        }

                        continue;
                    }

                    // check if socket is a client
                    map<int, Client*>::iterator client_it;
                    client_it = clients.find(pfd.fd);

                    if(client_it != clients.end())
                    {
                        // get client
                        Client* client = client_it->second;

                        // check disconnect event
                        if(pfd.revents&(POLLNVAL|POLLHUP|POLLERR))
                        {
                            delete client;
                            pollfds.erase(pfd_it);
                            clients.erase(client_it);
                            continue;
                        }

                        // check receiving event
                        if(pfd.revents&(POLLIN|POLLRDNORM))
                        {
                            int bytes = recv(client->socket_ctrl, client->buffer, CMD_BUFFER - 1, 0);

                            // check if recv was valid
                            if(bytes <= 0)
                            {
                                // socket was dropped
                                delete client;
                                pollfds.erase(pfd_it);
                                clients.erase(client_it);
                                continue;
                            }

                            // handle commands at a basic level
                            string data(client->buffer);
                            data.resize(bytes - 2);

                            // pretend we didn't see a blank line
                            if(data.empty())
                            {
                                continue;
                            }

                            stringstream in(data);

                            string cmd, params, param;
                            in >> cmd;

                            while(in >> param)
                            {
                                if(!params.empty())
                                {
                                    params += ' ';
                                }

                                params += param;
                            }

                            // capitalize command string internally
                            transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

                            // handle client command
                            client->handle_command(&commands, cmd, params);
                            continue;
                        }

                        continue;
                    }
                }
            }
        }
    }

    // Close sockets.
    for(map<int, Client*>::iterator client_it = clients.begin(); client_it != clients.end(); client_it++)
    {
        Client* client = client_it->second;
        delete client;
    }

    closesocket(server);
    sysThreadExit(0);
}