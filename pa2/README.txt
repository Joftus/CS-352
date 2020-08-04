TO BUILD
	make clean
	make

TO RUN (running requires user input for buffer size)
	./encrypt infile1 outfile1
	./encrypt infile2 outfile2

Project Summary
	Basic text file encryptor focused on a-z. Creates 5 threads (reader, writer, encryptor, input_counter, output_counter)
	so each stage of the encryption can run concurrently and use the limited buffer space optimally. Race conditions are stopped
	by using the c semaphore library. The program will display the character count (a-z) of both the input and the resulting
	output file. The only non-vanilla data structure used was a basic Queue implementation with nodes to store data such as
	the character it represents, the next node in the Queue, if it has been viewed by the counter, and finally if it has been encrypted.
	I broke the program into "blocks", marked by single line comments (ctrl + f: "BLOCK"), because the line count > 400 and I had a previous
	implementation fail using a separate c file for each thread.
