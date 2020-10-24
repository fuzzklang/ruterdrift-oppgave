#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>

#define SUCCESS 0
#define FAILURE -1
#define CRITICAL_FAILURE -2
#define TRUE 1
#define FALSE 0

#define CLR_RED "\x1B[1;31m"
#define CLR_GREEN "\x1B[0;32m"
#define CLR_YELLOW "\x1B[0;33m"
#define CLR_NRM "\x1B[0m"

#define BLOCK_MAX_SIZE 256
#define DESC_MAX_LEN 248
#define MAX_CONNECTIONS 10


struct router {
		unsigned int routerID;
		unsigned char flag;
		unsigned char desc_len;
		char description[DESC_MAX_LEN];
		struct router *connections[MAX_CONNECTIONS];
}__attribute__((__packed__));


/* File functions */
FILE *open_file(char filename[], char mode[]);
void get_to_next_router_info_block(FILE *fh);
int get_num_from_stream(unsigned int* number, FILE *fh);
int write_to_file(FILE *fh);
int write_connections_to_file(FILE *fh);

/* Router/routing config functions */
struct router *create_router(FILE *fh);
int create_all_routers(struct router **dest, FILE *fh, int N);
struct router *get_router(unsigned int routerID);
int is_connected(struct router *r, unsigned int id);
int add_connection(unsigned int fromID, unsigned int toID);
int set_connection(struct router *from, struct router *to);
int set_all_connections(FILE *fh);
unsigned char bit_pos_on(unsigned char flag, unsigned char bit_pos);
unsigned char bit_pos_off(unsigned char flag, unsigned char bit_pos);
unsigned char change_top_four_bits(unsigned char flag, unsigned char val);
int set_flag(unsigned int routerID, unsigned char bit_pos, unsigned char val);
int set_model(unsigned int routerID, char *new_name);
int remove_router(unsigned int routerID);
void remove_all_routers();

/* Command interaction functions */
int run_command(char line[]);
int run_all_commands(FILE *fh);

/* Path search functions */
int get_idx_in_visited(unsigned int routerID, struct router *ptr_array[]);
int recursive_search(struct router *r_ptr,
					 unsigned int findID,
					 int *visited[],
					 struct router *ptr_array[],
					 unsigned int *path[],
					 int *path_size,
					 unsigned int **path_cur_ptr);
int exists_path(unsigned int fromID, unsigned int toID);

/* Printing, error handling and error printing*/
int error_flag_file(FILE *fh, char calling_function[]);
void cleanup_on_abort(FILE *router_file, FILE *commands_file);
void print_router_data(struct router *r);
void print_invalid_bit_pos(unsigned char bit_pos, unsigned int routerID);
void print_invalid_val(unsigned char bit_pos, unsigned char val, unsigned int routerID);
void print_invalid_routerID(unsigned int(routerID));
void print_path(unsigned int path[], unsigned int *path_cur_ptr);

/* Helper functions */
void print_sizeof_router();
void print_all_router_data(struct router **array, int N);
void print_visited_array(int visited[], struct router *ptr_array[]);

/* Global array of struct pointers */
struct router **router_array;

/* Number of router information blocks in file */
/* And actual number of routers during runtime */
int N;
int N_ROUTERS;

int main(int argc, char *argv[])
{
		/* Check number of arguments given */
		if (argc != 3) {
				printf("Usage: ./ruterdrift <router_descriptions> <commands.txt>\n");
				printf("Exiting\n");
				return EXIT_FAILURE;
		}

		/* Open files, exit on fail
		 * Close files when all operations (reading router-info and commands)
		 * are finished and before file is reopened to be written to. */
		FILE *router_file = open_file(argv[1], "rb");
		FILE *commands_file = open_file(argv[2], "r");
		if (!(router_file) || !(commands_file))
				return EXIT_FAILURE;

		/* Set N (number of routers in description file) */
		fread(&N, sizeof(int), 1, router_file);
		N_ROUTERS = N;

		/*
		 * Allocate memory to global array of pointers to router structs,
		 * and fill structs with info from the given file.
		 * Size depends on N, number of router info blocks in input file,
		 * with size of 8 bytes per pointer.
		 * Memory allocated is freed at end of main.
		 */
		int result;
		router_array = malloc(sizeof(struct router*) * N);
		create_all_routers(router_array, router_file, N);
		result = set_all_connections(router_file);
		if (result == CRITICAL_FAILURE) {
				fprintf(stderr, "\n%s*Critical error*%s: when setting connections between routers.", CLR_RED, CLR_NRM);
				fprintf(stderr, " Aborting program to avoid an invalid write to file.\n\n");
				cleanup_on_abort(router_file, commands_file);
				return EXIT_FAILURE;
				}

		/* print_all_router_data(router_array, N); /\* INFO *\/ */

		/*
		 * Run commands-file
		 * Exit during runtime if error detected when executing commands
		 * from 'kommando-fil'. This to avoid a write from possible inconsistent
		 * state to router_file.
		 */
		result = run_all_commands(commands_file);
		if (result == CRITICAL_FAILURE) {
				fprintf(stderr, "\n%s*Critical error*%s: during execution of commands.", CLR_RED, CLR_NRM);
				fprintf(stderr, " Aborting program to avoid an invalid write to file.\n\n");
				cleanup_on_abort(router_file, commands_file);
				return EXIT_FAILURE;
				}

		/* Close filehandles initialized at beginning of program */
		fclose(router_file);
		fclose(commands_file);

		/* Open router file for writing */
		/* Is closed in main just after write_to_file() is finished */
		router_file = open_file(argv[1], "wb");
		/* router_file = open_file("./output", "wb"); */  /* DEBUG */
		if (!(router_file))
				return EXIT_FAILURE;

		/* Write information in router_array to file and close file */
		write_to_file(router_file);
		fclose(router_file);

		/* Free all allocated memory to struct-pointers in global array */
		/* and free memory to array of these pointers. */
		/* Memory to router_array was allocated at beginning of main */
		remove_all_routers();
		free(router_array);

		if (result != SUCCESS)
				puts("\n-- Exited, possibly with unfinished commands --");
		else
				puts("\n-- Finished successfully --");
		return EXIT_SUCCESS;
}



/* --- FILE FUNCTIONS --- */

/*
 * Function returns filehandler to an opened file given by argument char[] filename.
 * Tests result, prints error message if fopen is unsuccessful
 * Filehandler is returned on success, NULL on failure
 */
FILE *open_file(char filename[], char mode[])
{
		FILE *fh = fopen(filename, mode);
		if (fh == NULL) {
				fprintf(stderr, "%sError%s when trying to open file called '%s':\n      ", CLR_RED, CLR_NRM, filename);
				perror("");
		}
		return fh;
}


/*
 * According to specification, if the max size of the information block is
 * used (256 bytes) there will always be at least 1 undefined byte before the
 * terminating 0 of the block (4 + 1 + 1 + 248 + 1 = 255).
 * This function assures that the filehandler points to the first byte of
 * the next block.
 */
void get_to_next_router_info_block(FILE *fh)
{
		char val;
		do {
				fread(&val, sizeof(char), 1, fh);
		} while (val != 0);
}


/*
 * Function reads the part of the router file <FILE *fh> where connections are
 * specified. Sets the number specified by the pointer <unsigned int* number>
 * according to number read in file.
 * Reads one unsigned int at a time, checking for errors or unexpected early EOF.
 * Returns FAILURE on too early eof or with ferror sat, SUCCESS otherwise.
*/
int get_num_from_stream(unsigned int* number, FILE *fh)
{
		int elems_read = fread(number, sizeof(unsigned int), 1, fh);

		if (elems_read != 1) {
				if (error_flag_file(fh, "get_num_from_stream"))
						return FAILURE;
				else if (feof(fh))
						return FAILURE;
		}
		return SUCCESS;
}


/*
 * Function writes information from program back to the file given as filename
 * argument <char filename[]>. It first writes the integer defining the number
 * routers described in file. It then writes the information blocks on the
 * routers before calling a function which writes information on all connections
 * to file.
 */
int write_to_file(FILE *fh)
{
		unsigned char term_byte = 0;
		struct router *r;

		fwrite(&N_ROUTERS, sizeof(int), 1, fh);
		for (int i = 0; i < N; i++) {
				r = *(router_array + i);
				if (r) {
						/* Writing the 7 first bytes directly from struct to file */
						/* since these are static/of unchanged size according to spec. */
						/* Then writing the prod/model. string (without terminating 0)*/
						/* And then writing a term_byte (0) */
						fwrite(r, sizeof(unsigned char), 6, fh);
						fwrite(r->description, sizeof(char), r->desc_len, fh);
						fwrite(&term_byte, sizeof(unsigned char), 1, fh);

						/* Checking error flag each iteration */
						if (error_flag_file(fh, "write_to_file"))
								return FAILURE;
				}
		}

		write_connections_to_file(fh);

		return SUCCESS;
}


/*
 * Writes information on all connections between routers
 * to file given as argument <FILE *fh>.
 */
int write_connections_to_file(FILE *fh)
{
		unsigned char term_byte = 0;
		unsigned int toID;
		unsigned int fromID;
		struct router *r;
		struct router **conn;

		/* For each router */
		for (int i = 0; i < N; i++) {
				r = *(router_array + i);
				if (r) {
						fromID = r->routerID;
						conn = r->connections;
						/* For each connection for current router */
						/* If it exists, write info on connection to file */
						for (int j = 0; j < MAX_CONNECTIONS; j++) {
								if (conn[j]) {
										toID = (*(conn + j))->routerID;
										fwrite(&fromID, sizeof(unsigned int), 1, fh);
										fwrite(&toID, sizeof(unsigned int), 1, fh);
										fwrite(&term_byte, sizeof(unsigned char), 1, fh);
								}
						}

						if (error_flag_file(fh, "write_connections_to_file"))
								return FAILURE;
				}
		}
		return SUCCESS;
}



/* --- ROUTER/ROUTING FUNCTIONS --- */

/*
 * Allocate memory to a struct. Read information block in file (given by FILE fh)
 * to this struct. Function leaves filehandler pointing to the next byte
 * just after the terminating 0 in each information block.
 */
struct router *create_router(FILE *fh)
{
		/* Allocate memory, size of struct router.
		 * Size (in bytes): 6 + (DESC_MAX_LEN * (sizeof char)) + (MAX_CONNECTIONS * (sizeof ptr))
		 * = 6 + 248 + 80 = 334
		 * Memory is freed in remove_router() */
		struct router *r = malloc(sizeof(struct router));
		if (!(r))
				perror("Error with malloc");

		/* Reads routerID, flag and desc_len to struct *r */
		fread(r, sizeof(char), 6, fh);
		if (error_flag_file(fh, "create_router"))
				return NULL;
		/* Read in description (producer/model) */
		fread(r->description, sizeof(char), r->desc_len, fh);
		if (error_flag_file(fh, "create_router"))
				return NULL;

		/* Initialize connections to NULL */
		for(int i = 0; i < MAX_CONNECTIONS; i++) {
				r->connections[i] = NULL;
		}

		/* Get file pointer to the end of information block*/
		get_to_next_router_info_block(fh);
		return r;
}


/*
 * Create structs for each struct router pointer in the array
 * <struct router **dest>. Information for each struct is provided
 * by <FILE *fh>.The number of struct pointers in array is given by <int N>.
*/
int create_all_routers(struct router **dest, FILE *fh, int N)
{
		for(int i = 0; i < N; i++) {
				*(dest + i) = create_router(fh);
		}
		return SUCCESS;
}


/* Returns pointer to router with the corresponding routerID */
struct router *get_router(unsigned int routerID)
{
		struct router *r = NULL;
		struct router *compare_router;
		/* While router still not found (r_ptr == NULL),
		 * and i is less than number of routers in router_array */
		for (int i = 0; i < N && r == NULL; i++) {
				compare_router = router_array[i];
				if (compare_router && routerID == compare_router->routerID)
				        r = compare_router;
		}
		if (!(r))
				printf("%sWarning%s: Could not find router with id %d.\n", CLR_RED, CLR_NRM, routerID);

		return r;
}


/*
 * Check if a router <struct router *r> is already connected
 * to router with id <unsigned int id>.
 * Returns TRUE if connected, FALSE otherwise.
 */
int is_connected(struct router *r, unsigned int id)
{
		struct router *neighbour;
		for (int i = 0; i < MAX_CONNECTIONS; i++) {
				neighbour = r->connections[i];
				if (neighbour) {
						if (neighbour->routerID == id) {
								return TRUE;
						}
				}
		}
		return FALSE;
}


/*
 * Functions adds a (one way) connection from the the router given by
 * <unsigned int fromID> to the router given by <unsigned int toID>.
 * (Ignores already established connections)
 */
int add_connection(unsigned int fromID, unsigned int toID)
{
		struct router *r = get_router(fromID);
		if (is_connected(r, toID) == FALSE) {
				set_connection(r, get_router(toID));
				return SUCCESS;
		} else {
				return FAILURE;
		}
}


/*
 * Function finds an empty (NULL) ptr in connection array of the
 * <from> struct, and sets this to the <to>-pointer.
 * [A weakness with this function so far is that it does not check
 * for already existing connections between two routers, thus it can
 * create a new connection between two routers already connected.]
 */
int set_connection(struct router *from, struct router *to)
{
		for(int i = 0; i < MAX_CONNECTIONS; i++) {
				if (from && from->connections[i] == NULL) {
						from->connections[i] = to;
						return SUCCESS;
				}
		}
		return FAILURE;
}


/*
 * Function gets information on connection from <FILE *fh> and
 * establishes all connections accordingly.
 * Prints warning and ignores router IDs which does not exist, but
 * exits with CRITICAL_FAILURE if information block is incorrectly formatted,
 * or if ferror is set during read from file (this to avoid inconsistent state
 * in program and when writing to file).
 */
int set_all_connections(FILE *fh)
{
		/* Return to main if N <= 0 (no connections to set) */
		if (N <= 0)
				return 0;

		unsigned char c;
		while (!(feof(fh))) {
				/* Get IDs of from- and to-router */
				unsigned int fromID, toID;
				if (get_num_from_stream(&fromID, fh) != SUCCESS) {
						if (!feof(fh)) {
								fprintf(stderr, "%sError%s: Critical failure in set_all_connections", CLR_RED, CLR_NRM);
								fprintf(stderr, " when calling get_num_from_stream. Expecting an id: %d\n", fromID);
								return CRITICAL_FAILURE;
						}
						return SUCCESS;
				}
				if (get_num_from_stream(&toID, fh) != SUCCESS) {
						if (!feof(fh)) {
								fprintf(stderr, "%sError%s: Critical failure in set_all_connections", CLR_RED, CLR_NRM);
								fprintf(stderr, " when calling get_num_from_stream. Expecting an id: %d\n", toID);
								return CRITICAL_FAILURE;
						}
						return SUCCESS;
				}
				/* fetch ptr to routers based on IDs */
				/* and set connections accordingly */
				if (set_connection(get_router(fromID), get_router(toID)) != SUCCESS)
						fprintf(stderr, "\n%sError%s: Something went wrong when setting a router connection\n", CLR_RED, CLR_NRM);

				/* Consume the following 0-byte, and check if next call to fgetc()
				 * results in EOF. If not eof, unget character for later processing */
				fgetc(fh);
				c = (unsigned char) fgetc(fh);
				if (!(feof(fh))) {
						ungetc(c, fh);
				}
		}
		return SUCCESS;
}


/*
 * Function takes current flag <unsigned char> and masks the value
 * (bitwise or) of bit position <unsigned char bit_pos> so that
 * bit position is switched to 1.
 * Returns the modified flag (unsigned char).
 */
unsigned char bit_pos_on(unsigned char flag, unsigned char bit_pos)
{
		/* printf("Received flag: 0x%x and bit pos %u\n", flag, bit_pos); */
		unsigned char mask;
		switch (bit_pos) {
		case (0x0): mask = 0x1; break;
		case (0x1): mask = 0x2; break;
		case (0x2): mask = 0x4; break;
		}
		/* printf("Flag: 0x%x, mask: 0x%x\n", flag, mask); */
		/* printf("Returning new flag: 0x%x\n", new_flag); */
		return flag | mask;
}


/*
 * Function takes current flag <unsigned char> and masks the value
 * (bitwise and) of bit position <unsigned char bit_pos> so that
 * the bit position is switched to 0.
 * Returns the modified flag (unsigned char).
 */
unsigned char bit_pos_off(unsigned char flag, unsigned char bit_pos)
{
		unsigned char mask;
		switch (bit_pos) {
		case (0x0): mask = 0xfe; break;
		case (0x1): mask = 0xfd; break;
		case (0x2): mask = 0xfb; break;
		}
		return (unsigned char) flag & mask;
}


/*
 * Function takes current flag <unsigned char flag> and returns a modified
 * flag where the value of 4 MSB's is set according to value <unsigned char val>.
 * Returns the modified flag (unsigned char).
 */
unsigned char change_top_four_bits(unsigned char flag, unsigned char val)
{
		flag = flag & 0x0f;  /* Reset 4 MSBs to 0 */
		val = val << 4;      /* Shift to left to match bit pos of flag's MSBs */
		flag = val | flag;   /* Mask flag with val (bitwise or) */
		return flag;         /* return modified_flag; */
}


/*
 * Function modifies the flag of the router given by <unsigned int routerID>.
 * Sets the bit position <unsigned char bit_pos> to that of the given
 * value <unsigned char val>.
 *
 * Valid flags to set: 0, 1, 2, 4.
 * Invald flag: 3.
 * Valid options for flag 0, 1 and 2: 0 or 1.
 * Valid options for flag 4: 0 to 15 (incl.).
 */
int set_flag(unsigned int routerID, unsigned char bit_pos, unsigned char val)
{
		struct router *r = get_router(routerID);

		if (bit_pos == 0 || bit_pos == 1 || bit_pos == 2) {
				/* Call mask functions (bit on/off) with flag and bit position as arguments. */
				/* Set flag of router to flag returned by function*/
				if (val == 0) {
						r->flag = bit_pos_off(r->flag, bit_pos);
				} else if (val == 1) {
						r->flag = bit_pos_on(r->flag, bit_pos);
				} else {
						print_invalid_val(bit_pos, val, routerID);
						return FAILURE;
				}
		} else if (bit_pos == 4) {
				/* Special handlig for 'endringsnummer' */
				if (val >= 0 && val <= 15) {
						r->flag = change_top_four_bits(r->flag, val);
				} else {
						print_invalid_val(bit_pos, val, routerID);
						return FAILURE;
				}
		} else {
				/* Else, bit_pos is invalid */
				print_invalid_bit_pos(bit_pos, routerID);
				return FAILURE;
		}
		return SUCCESS;
}


/*
 * Sets the producer/model string of router given by <unsigned int routerID>
 * to the string given by char *new_name.
 */
int set_model(unsigned int routerID, char *new_name)
{
		struct router *r = get_router(routerID);
		int str_len = strlen(new_name);
		strncpy(r->description, new_name, DESC_MAX_LEN);
		r->desc_len = str_len;
		return SUCCESS;
}


/*
 * Remove router given by <unsigned int routerID> from network.
 * First the function removes any connections from other routers
 * to the given router.
 * Then it frees all allocated memory in struct of router,
 * before freeing the struct itself.
 */
int remove_router(unsigned int routerID)
{
		/* Check if router actually exists */
		if (!(get_router(routerID))) {
				fprintf(stderr, "%sError%s: no router with ID %d\n", CLR_RED, CLR_NRM, routerID);
				return FAILURE;
		}

		/* Iterate through all routers and look for connection */
		/* If found, set to NULL */
		struct router *searched_router, *other_router;
		for(int i = 0; i < N; i++) {
				other_router = router_array[i];
				if (other_router) {
						for (int j = 0; j < MAX_CONNECTIONS; j++) {
								searched_router = other_router->connections[j];
								if (searched_router && searched_router->routerID == routerID) {
										/* printf("Removing %d from connections of %d\n", searched_router->routerID, other_router->routerID);  /\* DEBUG *\/ */
										other_router->connections[j] = NULL;
								}
						}
				}
		}

		/* Careful with order.
		 * Get copy of the pointer to given router struct.
		 * Set pointer in global router array to NULL.
		 * Free router struct pointed to by copied pointer. */
		struct router *r = get_router(routerID);
		for(int i = 0; i < N; i++) {
				if (router_array[i] && router_array[i]->routerID == routerID) {
						router_array[i] = NULL;
				}
		}
		free(r);
		/* Decrement count of actual routers */
		N_ROUTERS -= 1;
		return SUCCESS;
}


/*
 * Iterates through global router_array and calls
 * remove_router(<unsigned int routerID>) for each router,
 * to remove it from list and free allocated memory.
 */
void remove_all_routers()
{
		struct router *r;
		for (int i = 0; i < N; i++) {
				r = router_array[i];
				if (r) {
						remove_router(r->routerID);
				}
		}
}



/* --- COMMANDS FUNCTIONS ---  */
/* Functions for interacting with commands in 'kommando-fil' */

/*
 * Parses the line given as argument from the func 'run_all_commands',
 * initializes the relevant variables according to the command,
 * and calls the corresponding function for each command.
 * Does a lot of error checking. Returns FAILURE if command not run successfully,
 * which results in early termination of program in main (to avoid an invalid write
 * to router file).
 */
int run_command(char line[])
{
		/* Parse line */
		char *command;
		unsigned int routerID;
		unsigned char flag;
		unsigned char val;
		char *desc;
		unsigned int fromID;
		unsigned int toID;
		int succeeded = SUCCESS;

		command = strtok(line, " ");
		/* Check if token is NULL*/

		if (!command)
				return CRITICAL_FAILURE;

		if (strcmp(command, "print") == 0) {
				routerID = atoi(strtok(NULL, " "));
				if (!(get_router(routerID))) {
						print_invalid_routerID(routerID);
						succeeded = FAILURE;
				} else {
						printf("\nInformation – Router %d:\n", routerID);
						print_router_data(get_router(routerID));
				}

		} else if (strcmp(command, "sett_flag") == 0) {
				routerID = atoi(strtok(NULL, " "));
				flag = atoi(strtok(NULL, " "));
				val = atoi(strtok(NULL, " "));
				if (!(get_router(routerID))) {
						print_invalid_routerID(routerID);
						succeeded = FAILURE;
				} else {
						/* printf("\nSetting flag – router: %d, flag: 0x%x, changing to: 0x%x\n", routerID, flag, val); */
						succeeded = set_flag(routerID, flag, val);
				}

		} else if (strcmp(line, "sett_modell") == 0) {
				routerID = atoi(strtok(NULL, " "));
				if (!(get_router(routerID))) {
						print_invalid_routerID(routerID);
						succeeded = FAILURE;
				} else {
						desc = strtok(NULL, "\n");
						/* printf("\nSetting model – id: %d, new description: %s\n", routerID, desc); */
						succeeded = set_model(routerID, desc);
				}

		} else if (strcmp(line, "legg_til_kobling") == 0) {
				fromID = atoi(strtok(NULL, " "));
				toID = atoi(strtok(NULL, " "));
				if (!(get_router(fromID))) {
						print_invalid_routerID(fromID);
						succeeded = FAILURE;
				} else if (!(get_router(toID))) {
						print_invalid_routerID(toID);
						succeeded = FAILURE;
				} else {
						/* printf("\nAdding connection – from %d to %d\n", fromID, toID); */
						succeeded = add_connection(fromID, toID);
				}

		} else if (strcmp(line, "slett_router") == 0) {
				routerID = atoi(strtok(NULL, " "));
				if (!(get_router(routerID))) {
						print_invalid_routerID(routerID);
						succeeded = FAILURE;
				} else {
						/* printf("\nRemoving – router with id: %d\n", routerID); */
						succeeded = remove_router(routerID);
				}

		} else if (strcmp(line, "finnes_rute") == 0) {
				fromID = atoi(strtok(NULL, " "));
				toID = atoi(strtok(NULL, " "));
				if (!(get_router(fromID))) {
						print_invalid_routerID(fromID);
						succeeded = FAILURE;
				} else if (!(get_router(toID))) {
						print_invalid_routerID(toID);
						succeeded = FAILURE;
				} else {
						/* printf("\nLooking for a path – from %d to %d\n", fromID, toID); */
						succeeded = exists_path(fromID, toID);
				}

		} else {
				fprintf(stderr, "%sWarning:%s '%s' is not a valid command\n", CLR_RED, CLR_NRM, command);
				fprintf(stderr, "Check for a possible empty line in commands-file\n");
				return CRITICAL_FAILURE;
		}

		if (!(succeeded == 0)) {
				/* fprintf(stderr, "%sWarning:%s something went wrong during execution of a command.\n", CLR_RED, CLR_NRM); */
				return FAILURE;
		}

		return SUCCESS;
}


/*
 * Executes all commands found in file given as argument <FILE *fh>,
 * by passing the read line from file to run_command();
 * Allocating memory for each line in file to be read to.
 * Worst case is a 'sett_modell'-command with id equal to biggest
 * possible unsigned int value (10 digits), with a string of size 248
 * (as from specification).
 * Function handles commando-files both with and without one trailing newline.
 */
int run_all_commands(FILE *fh)
{
		/* (See function description for details on this malloc-call)
		 * Allocated memory is freed at end of this function
		 * or during cleanup if an error should occur. */
		int command_max_len = 272;
		char *line = malloc(sizeof(char) * command_max_len);
		char tmp = (char) fgetc(fh);
		/* Check if file is empty, or if error on read */
		if (tmp == EOF) {
				if (error_flag_file(fh, "run_all_commands")) {
						free(line);
						return CRITICAL_FAILURE;
				} else if (feof(fh)) {
						fprintf(stderr, "%sWarning%s: Commands-file is empty.\n", CLR_RED, CLR_NRM);
						free(line);
						return FAILURE;
				}
		} else {
				/* If not, unget fetched char */
				ungetc(tmp, fh);
		}
		int result;
		while (!(feof(fh))) {
				fgets(line, command_max_len, fh);
				if (error_flag_file(fh, "run_all_commands")) {
						free(line);
						return CRITICAL_FAILURE;
				}

				/* Check for error during command execution. */
				/* If so, do cleanup and return result (FAILURE or CRITICAL_FAILURE) */
				result = run_command(line);
				if (result == CRITICAL_FAILURE) {
						free(line);
						return result;
				}

				/* Since the last call might include and ending newline and
				 * not an EOF, check if next call to fgetc() results in EOF.
				 * If not eof, unget character for later processing */
				tmp = fgetc(fh);
				if (!(feof(fh))) {
						ungetc(tmp, fh);
				}
		}
		free(line);
		return SUCCESS;
}



/* --- PATH SEARCH FUNCTIONS --- */
/*
 * Searches through an array of pointers to struct, and finds the index
 * for the router with <unsigned int routerID>.
 * Assumes that each router in global router_array has a pointer to
 * pointing to it in ptr_array.
 */
int get_idx_in_visited(unsigned int routerID, struct router *ptr_array[])
{
		for(int i = 0; i < N_ROUTERS; i++)
				if (ptr_array[i]->routerID == routerID)
						return i;
		return FAILURE;
}

/*
 * Recursive function for finding a path between two nodes.
 * r_ptr is the from-router, findID is the ID of to-router.
 * visited[] is an array containing info on which nodes has already been visited,
 * and ptr_array is an array of pointers to structs. The index of a router in the ptr_array
 * corresponds to the index in visited[].
 * path is an array containing IDs of the routers visited (and can thus be used to print path if found).
 * path_size keeps track of the current allocated size of path (for it to to be readjusted with realloc upon need),
 * and path_cur_ptr is a pointer to a pointer, which points to next free idx in ptr_array to place IDs.
 * (It's a double pointer so that the original in pointer in the calling function 'exists_path' also is modified).
 */
int recursive_search(struct router *r_ptr,
					 unsigned int findID,
					 int *visited[],
					 struct router *ptr_array[],
					 unsigned int *path[],
					 int *path_size,
					 unsigned int **path_cur_ptr)
{
		/* Check if path is full, if so realloc */
		ptrdiff_t cur_size = (*path_cur_ptr) - *path;
		if (cur_size >= (*path_size) - 1) {
				/* puts("Doing a realloc!");                       /\* DEBUG *\/ */
				/* printf("Current path_size: %d\n", *path_size); /\* DEBUG *\/ */
				*path = realloc(*path, sizeof(unsigned int) * (*path_size) * 2);
				*path_cur_ptr = *path + cur_size;
				*path_size = *path_size * 2;
				if (!*path) {
						fprintf(stderr, "%sError%s: realloc in recursive search failed.", CLR_RED, CLR_NRM);
						fprintf(stderr, " Will likely result in SEGFAULT\n");
				}
		}

		/* In visited-array, set current node (r_ptr) as visited */
		int visited_idx = get_idx_in_visited(r_ptr->routerID, ptr_array);
		(*visited)[visited_idx] = TRUE;

		**path_cur_ptr = r_ptr->routerID;
		(*path_cur_ptr)++;

		/* printf("\nVisiting router %u\n", r_ptr->routerID);  /\* DEBUG *\/ */
		/* print_visited_array(*visited, ptr_array);           /\* DEBUG *\/ */
		/* print_path(*path, (*path_cur_ptr) - 1);             /\* DEBUG *\/ */

		if (is_connected(r_ptr, findID)) {
				/* Connection found */
				/* printf("\nIn IF: DIFF: %ld\n", *path_cur_ptr - *path);  /\* DEBUG *\/ */
				**path_cur_ptr = findID;
				return TRUE;
		} else {
				/* Recursive search through all connections of current pointer */
				struct router *next_router;
				int next_visited_idx;
				for (int i = 0; i < MAX_CONNECTIONS; i++) {
						next_router = r_ptr->connections[i];
						/* If not NULL, and next router not already visited */
						if (next_router) {
								next_visited_idx = get_idx_in_visited(next_router->routerID, ptr_array);
								if (!(*visited)[next_visited_idx]) {
										/* Recursive call */
										if(recursive_search(next_router, findID, visited, ptr_array, path, path_size, path_cur_ptr)) {
												return TRUE;
										}
								}
						}
				}
		}
		/* When unnesting, mark this router as not visited, */
		/* and decrement current position of pointer to path-array */
		(*path_cur_ptr)--;
		(*visited)[visited_idx] = FALSE;
		return FALSE;
}


/*
 * Makes a call to the recursive search-function  to find a path from
 * router given by <unsigned int fromID> to router given by <unsigned int toID>.
 * Returns TRUE if a path is found, and FALSE otherwise.
 */
int exists_path(unsigned int fromID, unsigned int toID)
{
		/* Initialize temporary arrays and pointers for search. */
		/* Allocated memory is freed at end of this function */
		int *visited = malloc(sizeof(int) * N_ROUTERS);  /* Allocated: 4 bytes * N_ROUTERS: 40 bytes if 10 routers.  */
		struct router **ptr_array = malloc(sizeof(struct router*) * N_ROUTERS);  /* Allocated: 80 bytes if 10 routers */
		memset(visited, FALSE, sizeof(int) * N_ROUTERS);

		int path_size = 16;  /* Will not work if < 2 */
		/* Allocated for path: Initially path_size * 4 bytes.
		 * During runtime array can be expanded with realloc in recursive search.
		 * Memory is not freed until end of this function.
		 */
		unsigned int *path = malloc(sizeof(unsigned int) * path_size);
		unsigned int *path_cur_ptr = path;

		/*
		 * Assign each pointer in global array to this array
		 * This is done because global array has size N (might contain NULL-pointers),
		 * but local array, ptr_array, must contain only valid router pointers.
		 */
		int idx = 0;
		for (int i = 0; i < N; i++) {
				if (router_array[i]) {
						ptr_array[idx++] = router_array[i];
				}
		}

		puts("\n- Path search -");
		if (recursive_search(get_router(fromID), toID, &visited, ptr_array, &path, &path_size, &path_cur_ptr)) {
				printf("%sFound a path%s from router %u to %u!\n", CLR_GREEN, CLR_NRM, fromID, toID);
				print_path(path, path_cur_ptr);
		} else {
				printf("%sCould not find a path%s from router %u to %u.\n", CLR_YELLOW, CLR_NRM, fromID, toID);
		}

		/* Frees temporary arrays allocated at beginning of this function */
		free(path);
		free(visited);
		free(ptr_array);
		return SUCCESS;
}


void print_path(unsigned int path[], unsigned int *path_cur_ptr)
{
		printf("Path: ");
		for (int i = 0; i < (path_cur_ptr - path); i++)
				printf("%u ", path[i]);
		puts("");
}



/* --- PRINTING, ERROR PRINTING and ERROR HANDLING ---*/

/*
 * Check error indicator for given file <FILE *fh>, and print error message if set.
 * Return TRUE if set and FALSE otherwise.
 */
int error_flag_file(FILE *fh, char calling_function[])
{
		if (ferror(fh)) {
				fprintf(stderr, "%sError%s when reading from or writing to file. ", CLR_RED, CLR_NRM);
				perror("");
				fprintf(stderr, "Calling function: %s\n", calling_function);
				return TRUE;
		} else {
				return FALSE;
		}
}


/*
 * Cleanup function which closes open files and frees allocated memory.
 * Is called only if set_connections or run_all_commands return a CRITICAL_FAILURE.
 */
void cleanup_on_abort(FILE *router_file, FILE *commands_file)
{
		fclose(router_file);
		fclose(commands_file);
		remove_all_routers();
		free(router_array);    /* Global array */
}


/*
 * Print all data in struct of router given as argument <struct router *r>.
 * Also prints which (one way) connections router has to other routers.
*/
void print_router_data(struct router *r)
{
		/* Print info on given router */
		printf("id:           %3d    0x%02x\n", r->routerID, r->routerID);
		printf("flag:                0x%02x\n", r->flag);
		printf("desc_len:     %3d    0x%02x\n", r->desc_len, r->desc_len);
		printf("Prod./model:   ");
		for(int i = 0; i < r->desc_len; i++)
				printf("%c", r->description[i]);

		/* Print out all connections */
		printf("\nConnected to:  ");
		for(int i = 0; i < MAX_CONNECTIONS; i++) {
				if (r->connections[i] != NULL)
						printf("%d ", r->connections[i]->routerID);
		}
		puts("");
}


/*
 * Function used in set_flag() to print error messages.
 */
void print_invalid_bit_pos(unsigned char bit_pos, unsigned int routerID)
{
		fprintf(stderr, "\n%sWarning%s: invalid bit_pos %u (0x%x)", CLR_RED, CLR_NRM, bit_pos, bit_pos);
		fprintf(stderr, " for flag in router %u passed to program. Ignoring.", routerID);
}

/*
 * Function used in set_flag() to print error messages.
 */
void print_invalid_val(unsigned char bit_pos, unsigned char val, unsigned int routerID)
{
		fprintf(stderr, "\n%sWarning%s: Trying to set bit_pos %u (0x%x)", CLR_RED, CLR_NRM, bit_pos, bit_pos);
		fprintf(stderr, " in router %u's flag to invalid value: %u (0x%x). Ignoring.\n", val, val, routerID);
}


void print_invalid_routerID(unsigned int(routerID))
{
		fprintf(stderr, "%sWarning%s: Asked to perform operation on nonexistent router %u.\n", CLR_RED, CLR_NRM, routerID);
}



/* --- HELPER FUNCTIONS --- */
/* Not necessary for functionality */

void print_all_router_data(struct router **array, int N)
{
		printf("\n=== INFO ALL ROUTERS ===\n");
		struct router *r;
		for(int i = 0; i < N; i++) {
				r = array[i];
				if (r) {
						printf("\n--- Router id nr. %d ---\n", r->routerID);
						print_router_data(r);
				}
		}
}


void print_sizeof_router()
{
		struct router r;
		printf("sizeof struct router: %ld\n", sizeof(r));
		printf("size of r.routerID: %ld\n", sizeof(r.routerID));
		printf("size of r.flag: %ld\n", sizeof(r.flag));
		printf("size of r.desc_len: %ld\n", sizeof(r.desc_len));
		printf("size of r.description: %ld\n", sizeof(r.description));
		printf("size of r.connections: %ld\n", sizeof(r.connections));
}


void print_visited_array(int visited[], struct router **ptr_array)
{
		printf("\nVisited[]:      ");
		for (int i = 0; i < N_ROUTERS; i++){
				printf("%2d ", visited[i]);
		}
		printf("\nID of routers:  ");
		for (int i = 0; i < N_ROUTERS; i++) {
				printf("%2d ", ptr_array[i]->routerID);
		}
		printf("\nidx in visited: ");
		/* Handles cases where ptrs are moved. Keeps printing sorted. */
		int cur_idx;
		for (int i = 0; i < N_ROUTERS; i++) {
				for (int j = 0; j < N_ROUTERS; j++) {
						cur_idx = get_idx_in_visited(ptr_array[i]->routerID, ptr_array);
						if (cur_idx == i) {
								printf("%2d ", cur_idx);
								break;
						}
				}
		}
		puts("");
}
