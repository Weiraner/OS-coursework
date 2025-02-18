#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//check if all characters on the board has been used
int allNonNegative(int a[26]){
	for(int i = 0; i < 26; i++){
		if(a[i] < 0){
			return 0;
		}
	}

	return 1;
}


int main(int argc, char *argv[]){
	
	if (argc != 3) {
		printf("Usage: ./letter-boxed <board_file> <dict_file>\n");
		return 1;
	}

	//record the location of the letter
	int letter_location[26];

	//initialize the elements
	for (int i = 0; i < 26; i++){
		letter_location[i] = 0;
	}

	//open boardfile
	FILE *board_file = fopen(argv[1], "r");
	if (board_file == NULL){
	        return 1;
	}

	//read every line in board and record location of letter
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int row = 1;

	while((read = getline(&line, &len, board_file)) != -1){
		//pass blank lines
		if(read == 1)continue;

		//traverse every letter in the line
		for(int i = 0; i < read-1; i++){
			int num = (int)line[i] - 97;
			//if the letter appears before
			if(letter_location[num] != 0){
				printf("Invalid board\n");
				return 1;
			}
			//record location of the letter
			letter_location[num]=-row;
		}
		row++;
	}
	
	//close the file
	if(fclose(board_file) != 0){
		return 1;
	}

	row--;
	//if the board has less than 3 sides
	if(row<3){
		printf("Invalid board\n");
		return 1;
	}

        //check every input line
	char end = '\0';

        while((read = getline(&line, &len, stdin)) != -1){
                //pass blank lines
                if(read == 1)continue;

		//Check if the first letter is on the board
		if(letter_location[line[0]-97] == 0){
			printf("Used a letter not present on the board\n");
			return 0;
		}
		else if(letter_location[line[0]-97] < 0){
			letter_location[line[0]-97] *= -1;
		}


		//check if the head of current line and the
		//end of previous line are the same.
		if(end != '\0'){
			if(line[0]!=end){
				printf("First letter of word does not match last letter of previous word\n");
				return 0;
			}
		}

                //traverse to check if a letter not present is used
		//or sameside letter used consecutively
                for(int i = 1; i < read-1; i++){
			
			int line_prev = letter_location[(int)line[i-1]-97];
			int line_cur = letter_location[(int)line[i]-97];

			//letter not present usage
			if(line_cur == 0){
				printf("Used a letter not present on the board\n");
				return 0;
			}

			//mark that the current character has appeared
			if(line_cur < 0){
				line_cur = -line_cur;
				letter_location[(int)line[i]-97] = line_cur;
			}

			//sameside letter used consecutively
			if(line_cur == line_prev){
				printf("Same-side letter used consecutively\n");
				return 0;
			}
                }

		//check if the word is in the dictionary
		
		//open dictionary file
		FILE *dict_file = fopen(argv[2],"r");
		if(dict_file == NULL){
			return 1;
		}

		char *dic_line = NULL;
		size_t dic_len = 0;
		ssize_t dic_read;
		int found = 0;

		//check every line in dictionary
		while ((dic_read = getline(&dic_line, &dic_len, dict_file)) != -1){
			if(strcmp(line, dic_line) == 0)found =1;
		}

		if(found == 0){
			printf("Word not found in dictionary\n");
			return 0;
		}

		//free the buffer
		free(dic_line);
		//close the file
		if(fclose(dict_file) != 0)return 1;

		//check if all characters have been used
		if(allNonNegative(letter_location) == 1){
			printf("Correct\n");
			return 0;
		}

		//record the end of current line
		end = line[read-2];
	}
        //free the buffer
        free(line);

	//not all letters are used
	printf("Not all letters used\n");
	return 0;
}

