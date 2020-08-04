#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

// Global Definitions
#define UNIQUE_CHARACTERS 26  // caps are changed by reader
#define LOG 1
#define TRUE 1
#define FALSE 0



// Data Structures
// Used a FILO list to maintain buffer data, functionality of Queues are well documented
typedef struct node node;
struct node {
	char data;
	// marked means a queue has counted it already, protected refers its encryption status
	int marked, protected;
	// pointer to node next in queue
	node *child;
};

typedef struct {
	// keep track of bounds of queue
	node *start, *end;
	// store max buffer size and compare it to current_size to check for space in the buffer
	int max_size, current_size;
} Queue;



// Header
void *reader(void *thread_args);
void *input_counter(void *thread_args);
void *encryptor(void *thread_args);
void *output_counter(void *thread_args);
void *writer(void *thread_args);
char helper(char c, int *s);
int enqueue(Queue *q, char c);
node *dequeue(Queue *q);
void display_input_stats();
void display_output_stats();



// Universal Information (used prefix's for clarity i.e. "lock_" and "file_")
FILE *file_input, *file_output;
// buffers for moving data through all 5 threads
Queue input_buffer, output_buffer;
// semaphore == lock, # of locks > # of threads because the encryptor thread requires 2, syntax: lock_{thread}
sem_t lock_reader, lock_writer, lock_encryptor_input, lock_encryptor_output, lock_input_counter, lock_output_counter;
// 2 int arrays to track character counts of i/o files
int input_counter_arr[UNIQUE_CHARACTERS], output_counter_arr[UNIQUE_CHARACTERS], buffer_length;



// START OF DRIVER BLOCK


/* main
	+ Obtain the input and output files from the command line. If the number of command line arguments is incorrect, exit after displaying a message about correct usage.
	+ Prompt the user for the buffer sizeN.
	+ Initialize shared variables. This includes allocating appropriate data structures for the input and output buffers, and appropriate data structures to count the number of occurrences of each letter in the input and output files. You may use any data structure capable of holding exactly N characters for the input and output buffers.
	+ Create the other threads.
	+ Wait for all threads to complete.
	- Display the number of occurrences of each letter in the input and output files
*/
int main(int argc, char *argv[]) {
	int i;
	pthread_t threads[5];
	char user_input[64];
	
	if (LOG) printf("running process...\n");
	// check if files were provided and validity
	if (argc != 3 || fopen(argv[1], "r") == NULL) {
		printf("!FILES NOT PROVIDED OR INVALID!\n\n");
		exit(1);
	}
	// get file ptrs
	file_input = fopen(argv[1], "r");
	file_output = fopen(argv[2], "w");
	
	// get buffer size from user
	printf("  buffer size: ");
	fflush(stdout);	
	fgets(user_input, 64, stdin);
	// format input and init both queues w/ provided value
	user_input[strlen(user_input) - 1] = '\0';
	buffer_length = atoi(user_input);
	input_buffer.max_size = buffer_length;
	input_buffer.current_size = 0;
	output_buffer.max_size = buffer_length;
	output_buffer.current_size = 0;
	
	// init locks
	sem_init(&lock_reader, FALSE, TRUE);
	sem_init(&lock_input_counter, FALSE, FALSE);
	sem_init(&lock_encryptor_input, FALSE, FALSE);
	sem_init(&lock_encryptor_output, FALSE, TRUE);
	sem_init(&lock_output_counter, FALSE, FALSE);	
	sem_init(&lock_writer, FALSE, FALSE);
	
	// create threads
	pthread_create(&threads[0], NULL, reader, NULL);
	pthread_create(&threads[1], NULL, input_counter, NULL);
	pthread_create(&threads[2], NULL, encryptor, NULL);
	pthread_create(&threads[3], NULL, output_counter, NULL);
	pthread_create(&threads[4], NULL, writer, NULL);
	
	// join threads on completion
	for (i=0; i<5; i++)
		pthread_join(threads[i], NULL);
	
	// show stats
	display_input_stats();
	display_output_stats();
	if (LOG) printf("\nPROGRAM COMPLETE!\n\n\n");
	return 0;
}



// END OF DRIVER BLOCK
// START OF THREADS BLOCK



/* reader
	+ The reader thread is responsible for reading from the input file (specified by the first argument on the command line) one character at a time, and placing the characters in the input buffer.
	+ Each buffer item corresponds to a character.
	+ Note that the reader thread may need to block until other threads have consumed data from the input buffer.
	- Specifically, a character in the input buffer cannot be overwritten until the encryptor thread and the input counter thread have processed the character.
	+ The reader continues until the end of the input file is reached.
*/
void *reader(void *thread_args) {
	char in;

	// get first char to init loop
	in = fgetc(file_input);
	while (TRUE) {
		// blocking
		sem_wait(&lock_reader);
		if (enqueue(&input_buffer, in) == TRUE) {
			// release "lock"
			sem_post(&lock_input_counter);
			if (in == EOF)
				break;
			else
				in = fgetc(file_input);
		}
	}
}



/* writer
	+ The writer thread is responsible for writing the encrypted characters in the output buffer to the output file (specified by the second argument on the command line).
	+ Note that the writer may need to block until an encrypted character is available in the buffer.
	- The writer continues until it has written the last encrypted character.
	LOOK FOR EOF!
*/
void *writer(void *thread_args) {
	node *out;
	
	while (TRUE) {
		// blocking
		sem_wait(&lock_writer);
		out = output_buffer.start;
		if (out->marked == TRUE) {
			// remove "out" from output buffer
			dequeue(&output_buffer);
			// end of file check			
			if (out->data == EOF)
				break;
			// put encrypted char into output file
			fputc(out->data, file_output);
			// system op to clear writer output "stream"
			fflush(file_output);
			out = out->child;
		}
		// release "lock"
		sem_post(&lock_encryptor_output);
	}
}



/* encryptor
	+ The encryption thread consumes one character at a time from the input buffer, encrypts it, and places it in the output buffer.
	+ The encryption algorithm to use is given below.
	+ Of course, the encryption thread may need to wait for an item to become available in the input buffer, and for a slot to become available in the output buffer.
	+ Note that a character in the output buffer cannot be overwritten until the writer thread and the output counter thread have processed the character.
	+ The encryption thread continues until all characters of the input file have been encrypted.
	+ The encryption algorithm is fairly simple (and is very easy to crack).
	- Only alphabetic characters (i.e., 'A'..'Z' and 'a'..'z') are changed; all other characters are simply copied to the output file. 
	+ The algorithm either increments, decrements, or does not touch alphabetic characters. The encryption algorithm is as follows.
		1. s = 1;
		2. Get next character c.
		3. if c is not a letter then goto (7).
		4. if (s==1) then increase c with wraparound (e.g., 'A' becomes 'B', 'c' becomes 'd', 'Z' becomes 'A', 'z' becomes 'a'), set s=-1, and goto (7).
		5. if (s==-1) then decrease c with wraparound (e.g., 'B' becomes 'A', 'd' becomes 'c', 'A' becomes 'Z', 'a' becomes 'z'), set s=0, and goto (7).
		6. if (s==0), then do not change c, and set s=1.
		7. Encrypted character is c.
		8. If c!=EOF then goto (2).
	- a == A
*/
void *encryptor(void *thread_args) {
	node *in, *mover;
	int state;
	
	// set base encryptor state to 1
	state = 1;
	while (TRUE) {
		// blocking
		sem_wait(&lock_encryptor_input);
		in = input_buffer.start;
		while (in != NULL) {
			// process the node using helper() function
			if (in->marked == TRUE && in->protected != TRUE) {
				if (in->data != EOF && in->data != '\n')
					in->data = helper(in->data, &state);
				in->protected = TRUE;
				break;
			}
			// move to next node
			in = in->child;
		}
		if (input_buffer.current_size > 0 && input_buffer.start->protected == TRUE) {
			mover = dequeue(&input_buffer);
			// free reader "lock"
			sem_post(&lock_reader);
		}
		// blocking
		sem_wait(&lock_encryptor_output);
		// put encrypted char into output queue to be processed
		enqueue(&output_buffer, mover->data);
		// free output counter "lock"
		sem_post(&lock_output_counter);
		// end of file check
		if (mover->data == EOF)
			break;
	}
}



/* input counter
	+ The input counter thread simply counts occurrences of each letter in the input file by looking at each character in the input buffer.
	+ Of course, the input counter thread will need to block if no characters are available in the input buffer.
*/
void *input_counter(void *thread_args) {
	node *in;

	while (TRUE) {
		// blocking
		sem_wait(&lock_input_counter);
		in = input_buffer.start;
		while (in != NULL) {
			// if it hasn't been counted, count it
			if (in->marked == FALSE) {
				// if it is char a - z add it to counter array
				if (in->data >= 97 && in->data <= 122)
					input_counter_arr[in->data - 97] += 1;
				in->marked = 1;
				// free "lock"
				sem_post(&lock_encryptor_input);
				// end of file check
				if (in->data == EOF)
					return NULL;
				else
					break;					
			}
			// if marked, move to next in queue
			else
				in = in->child;
		}
	}	
}



/* output counter
	+ The output counter thread simply counts occurrences of each letter in the output file by looking at each character in the output buffer.
	+ Of course, the output counter thread will need to block if no characters are available in the output buffer.
*/
void *output_counter(void *thread_args) {
	node *out;
	
	while (TRUE) {
		// blocking
		sem_wait(&lock_output_counter);
		out = output_buffer.start;
		while (out != NULL) {
			if (out->marked != TRUE) {
				// if it is char a - z add it to counter array
				if (out->data >= 97 && out->data <= 122)
					output_counter_arr[out->data - 97] += 1;
				out->marked = TRUE;
				// free "lock"
				sem_post(&lock_writer);
				// end of file check
				if (out->data == EOF)
					return NULL;
				else
					break;
			}
			// if already marked, move to next
			else
				out = out->child;
		}
	}
}



// END OF THREADS BLOCK
// START OF QUEUE UTILITY BLOCK


// basic queue function, minimal documentation
int enqueue(Queue *queue, char data) {
	node *add;
	
	if (queue->current_size == queue->max_size)
		return 0; 
	add = (node*) malloc(sizeof(node));
	// load data to new node data structure
	add->marked = FALSE;
	add->protected = FALSE;
	add->data = data;
	// check if queue is empty, else append to end of queue
	if (queue->current_size == 0) {
		queue->start = add;
		queue->end = add;
	} 
	else {
		queue->end->child = add;
		queue->end = add;
	}
	queue->current_size = queue->current_size + 1;
	// return 1 == success and satisfies if statements contingent on success of this function
	return 1;
}

// basic queue function, minimal documentation
node *dequeue(Queue *queue) {
	node *remove;
	
	// error check
	if (queue->current_size == 0)
		return NULL;
	// remove node from queue by moving its link "start" to its child
	remove = (node*) malloc(sizeof(node));
	remove->data = queue->start->data;
	remove->marked = queue->start->marked;
	remove->protected = queue->start->protected;
	queue->start = queue->start->child;
	// check if queue is now empty
	if (queue->current_size == 1)
		queue->end = NULL;
	queue->current_size--;
	
	return remove;
}



// END OF QUEUE UTILITY BLOCK
// START OF ENCRYPTOR UTILITY BLOCK



// logic function that matches the specs of the encryption algo given
char helper(char in, int *state) {
	if (in >= 65 && in <= 90)
		in += 32;
	if (in >= 97 && in <= 122) {
		if (*state == -1) {
			*state = 0;
			if (in == 97)
				in = 122;
			else
				in = in - 1;
		}
		else if (*state == 0)
			*state = 1;
		else if (*state == 1) {
			*state = -1;
			if (in == 122)
				in = 97;
			else
				in = in + 1;
		}
	}
	return in;
}



// END OF ENCRYPTOR UTILITY BLOCK
// START OF MAIN UTILITY BLOCK



void display_input_stats() {
	int i;
	
	printf("\n\n\n-------| Input Statistics |-------\n");
	printf("  char: count\n");
	for(i = 0; i < UNIQUE_CHARACTERS; i++) {
		if (input_counter_arr[i] > 0 && ((char) i) != '\n')
			printf("  %c: %d \n",(char) (i + 97), input_counter_arr[i]);
	}
}

void display_output_stats() {
	int i;

	printf("\n\n\n-------| Output Statistics |-------\n");
	printf("  char: count\n");
	for(i = 0; i < UNIQUE_CHARACTERS; i++) {
		if (output_counter_arr[i] > 0  && ((char) i) != '\n')
			printf("  %c: %d \n",(char) (i + 97), output_counter_arr[i]);
	}
}



// END OF MAIN UTILITY BLOCK
// END OF FILE