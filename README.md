roboserve
=========

This library provides lightweight web server controls for robotics.

By adding a few lines of code you can have a simple but skinnable web interface exposed directly to variables in your running C program. Currently offers remote control of any number of floats, ints or strings.

Running the below program will cause a webserver to open on your device at port 80. It has simple file serving capabilities but also can export C variables as a web form. You can style this with css for what ever effects you desire.

Currently supports exportings C strinfgs, ints and floats.

Example
=======
#include <roboserve.h>

int main(argc, argc)
{
	float im_remote_controlled;
	struct server * my_server = new_server();

// Set port to listen on, defaults to 8000
	server->portno = 80;

// Add a new webpage available at http://<ip address>/settings/	
	struct message * my_message= add_message(my_server, "settings");

// Put our float on the web,
	add_float_box(my_message, "trim", &im_remote_controlled);

// Start the server.
	server_start(my_server);

	while(1)
	{
		printf("%f\n", im_remote_controlled);
	}
}


