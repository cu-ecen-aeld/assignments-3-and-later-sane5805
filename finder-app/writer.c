#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]) {

    openlog("log", LOG_PID, LOG_USER);
    
    // Check if both arguments are provided
    if (argc != 3) {
        // fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        syslog(LOG_ERR, "Usage: ./writer <writefile> <writestr>\n");
        
        //exit(EXIT_FAILURE);
        return 1;
    }

    // Assign arguments to variables
    char *writefile = argv[1];
    char *writestr = argv[2];

    // Check if writefile is not empty
    if (writefile == NULL || strlen(writefile) == 0) {
        //fprintf(stderr, "Error: 'writefile' argument is empty.\n");
        syslog(LOG_ERR, "Error: 'writefile' argument is empty.\n");
        
        //exit(EXIT_FAILURE);
        return 1;
    }

    // Check if writestr is not empty
    if (writestr == NULL || strlen(writestr) == 0) {
        //fprintf(stderr, "Error: 'writestr' argument is empty.\n");
        syslog(LOG_ERR, "Error: 'writestr' argument is empty.\n");
        
        //exit(EXIT_FAILURE);
        return 1;
    }

    // Open the file for writing
    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: Failed to open file '%s' for writing.\n", writefile);
        
        //exit(EXIT_FAILURE);
        return 1;
    }

    // Write the content to the file
    if (fprintf(file, "%s", writestr) < 0) {
        fprintf(stderr, "Error: Failed to write to file '%s'.\n", writefile);
        fclose(file);
        
        //exit(EXIT_FAILURE);
        return 1;
    }

    // Close the file
    fclose(file);

    // Log the message with syslog
    //openlog("writer", LOG_PID, LOG_USER);
    //syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    closelog();

    // Exit with success status
    //exit(EXIT_FAILURE);
    return 1;
}
