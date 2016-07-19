#include <Python.h>
#include <stdbool.h>

// Use for returning errors
#define err_size 500
#define done_parsing  (void *)1

// Our whitepspace chars
char whitespace[4] = " \n\t\v";

// A parser struct to keep track of state
typedef struct {
    char * source;
    char * full_data;
    char * token;
    long index;
    long length;
    char last_delineator;
} parser_data;

// Initialize the parser
parser_data parser = {NULL, NULL, done_parsing, 0, 0, ' '};

void reset_parser(parser_data * parser){

    if (parser->source != NULL){
        free(parser->full_data);
        parser->source = NULL;
    }

    parser->full_data = NULL;
    if (parser->token != done_parsing){
        free(parser->token);
    }
    parser->token = NULL;
    parser->index = 0;
    parser->length = 0;
    parser->last_delineator = ' ';
}

void print_parser_state(parser_data * parser){
    if (parser->source){
        printf("parser(%s):\n", parser->source);
    } else {
        printf("parser(NULL)\n");
        return;
    }
    printf(" Pos: %lu/%lu\n", parser->index, parser->length);
    printf(" Last delim: '%c'\n", parser->last_delineator);
    printf(" Last token: '%s'\n\n", parser->token);
}

/* Return the index of the first match of needle in haystack, or -1 */
long get_index(char * haystack, char * needle, long start_pos){

    haystack += sizeof(char) * start_pos;
    char * start = strstr(haystack, needle);

    // Return the end if string not found
    if (!start){
        return -1;
    }

    // Calculate the length into start is the new word
    long diff = start - haystack;
    return diff;
}

void get_file(char *fname, parser_data * parser){
    //printf("Parsing: %s\n", fname);

    printf("%p\n", done_parsing);
    reset_parser(parser);

    // Open the file
    FILE *f = fopen(fname, "rb");
    if (!f){
        PyErr_SetString(PyExc_IOError, "Could not open file.");
        return;
    }

    // Determine how long it is
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Allocate space for the file in RAM and load the file
    char *string = malloc(fsize + 1);
    if (fread(string, fsize, 1, f) != 1){
        PyErr_SetString(PyExc_IOError, "Short read of file.");
        return;
    }

    fclose(f);
    // Zero terminate
    string[fsize] = 0;

    parser->full_data = string;
    parser->length = fsize;
    parser->source = fname;
    parser->index = 0;
    parser->last_delineator = ' ';
}

/* Determines if a character is whitespace */
bool is_whitespace(char test){
    int x;
    for (x=0; x<sizeof(whitespace); x++){
        if (test == whitespace[x]){
            return true;
        }
    }
    return false;
}

/* Returns the index of the next whitespace in the string. */
long get_next_whitespace(char * string, long start_pos){

    long pos = start_pos;
    while (string[pos] != '\0'){
        if (is_whitespace(string[pos])){
            return pos;
        }
        pos++;
    }
    return pos;
}

/* Scan the index to the next non-whitespace char */
void pass_whitespace(parser_data * parser){
    while ((parser->index < parser->length) &&
            (is_whitespace(parser->full_data[parser->index]))){
        parser->index++;
    }
}

/* Determines if we are done parsing. */
bool check_finished(parser_data * parser){
    if (parser->index == parser->length){
        free(parser->token);
        parser->token = done_parsing;
        return true;
    }
    return false;
}

bool check_multiline(parser_data * parser, long length){
    long x;
    for (x=parser->index; x <= parser->index+length; x++){
        if (parser->full_data[x] == '\n'){
            return true;
        }
    }
    return false;

}

/* Returns a new token char * */
char * update_token(parser_data * parser, long length){

    if (parser->token != done_parsing){
        free(parser->token);
    }
    parser->token = malloc(length+1);
    //printf("index %ld par_len %ld my_len %ld\n", parser->index, parser->length, length);
    memcpy(parser->token, &parser->full_data[parser->index], length);
    parser->token[length] = '\0';

    // Figure out what to set the last delineator as
    if (parser->index == 0){
        parser->last_delineator = ' ';
    } else {
        char ld = parser->full_data[parser->index-1];
        if ((ld == '\n') && (parser->index > 2) && (parser->full_data[parser->index-2] == ';')){
            parser->last_delineator = ';';
        } else if ((ld == '"') || (ld == '\'')){
            parser->last_delineator = ld;
        } else {
            parser->last_delineator = ' ';
        }
    }

    parser->index += length + 1;
    return parser->token;
}


// Get the current line number
long get_line_number(parser_data * parser){
    long num_lines = 0;
    long x;
    for (x = 0; x < parser->index; x++){
        if (parser->full_data[x] == '\n'){
            num_lines++;
        }
    }
    return num_lines + 1;
}

char * get_token(parser_data * parser){

    // Reset the delineator
    parser->last_delineator = '\0';

    // Set up a tmp str pointer to use for searches
    char * search;
    // And an error char array
    char err[err_size] = "Unknown error.";

    // Nothing left
    if (parser->token == done_parsing){
        return parser->token;
    }

    // Skip whitespace
    pass_whitespace(parser);

    // Stop if we are at the end
    if (check_finished(parser)){
        return parser->token;
    }

    // See if this is a comment - if so skip it
    if (parser->full_data[parser->index] == '#'){
        search = "\n";
        long length = get_index(parser->full_data, search, parser->index);

        // Handle the edge case where this is the last line of the file and there is no newline
        if (length == -1){
            parser->token = done_parsing;
            return parser->token;
        }

        // Skip to the next non-comment
        parser->index += length;
        return get_token(parser);
    }

    // See if this is a multiline comment
    if ((parser->length - parser->index > 1) && (parser->full_data[parser->index] == ';') && (parser->full_data[parser->index+1] == '\n')){
        search = "\n;";
        long length = get_index(parser->full_data, search, parser->index);

        // Handle the edge case where this is the last line of the file and there is no newline
        if (length == -1){
            snprintf(err, err_size-1, "Invalid file. Semicolon-delineated value was not terminated. Error on line: %ld", get_line_number(parser));
            PyErr_SetString(PyExc_ValueError, err);
            parser->token = NULL;
            return parser->token;
        }

        parser->index += 2;
        return update_token(parser, length-1);
    }

    // Handle values quoted with '
    if (parser->full_data[parser->index] == '\''){
        search = "'";
        long end_quote = get_index(parser->full_data, search, parser->index + 1);

        // Handle the case where there is no terminating quote in the file
        if (end_quote == -1){
            snprintf(err, err_size-1, "Invalid file. Single quoted value was not terminated. Error on line: %ld", get_line_number(parser));
            PyErr_SetString(PyExc_ValueError, err);
            parser->token = NULL;
            return parser->token;
        }

        // Make sure we don't stop for quotes that are not followed by whitespace
        while ((parser->index+end_quote+2 < parser->length) && (!is_whitespace(parser->full_data[parser->index+end_quote+2]))){
            long next_index = get_index(parser->full_data, search, parser->index+end_quote+2);
            if (next_index == -1){
                PyErr_SetString(PyExc_ValueError, "Invalid file. Single quoted value was never terminated at end of file.");
                parser->index = parser->length;
                parser->token = NULL;
                return parser->token;
            }
            end_quote += next_index + 1;
        }

        // See if the quote has a newline
        if (check_multiline(parser, end_quote)){
            snprintf(err, err_size-1, "Invalid file. Single quoted value was not terminated on the same line it began. Error on line: %ld", get_line_number(parser));
            PyErr_SetString(PyExc_ValueError, err);
            parser->token = NULL;
            return parser->token;
        }

        // Move the index 1 to skip the '
        parser->index++;
        return update_token(parser, end_quote);
    }

    // Handle values quoted with "
    if (parser->full_data[parser->index] == '\"'){
        search = "\"";
        long end_quote = get_index(parser->full_data, search, parser->index + 1);

        // Handle the case where there is no terminating quote in the file
        if (end_quote == -1){
            snprintf(err, err_size-1, "Invalid file. Double quoted value was not terminated. Error on line: %ld", get_line_number(parser));
            PyErr_SetString(PyExc_ValueError, err);
            parser->token = NULL;
            return parser->token;
        }

        // Make sure we don't stop for quotes that are not followed by whitespace
        while ((parser->index+end_quote+2 < parser->length) && (!is_whitespace(parser->full_data[parser->index+end_quote+2]))){
            long next_index = get_index(parser->full_data, search, parser->index+end_quote+2);
            if (next_index == -1){
                PyErr_SetString(PyExc_ValueError, "Invalid file. Double quoted value was never terminated at end of file.");
                parser->index = parser->length;
                parser->token = NULL;
                return parser->token;
            }
            end_quote += next_index + 1;
        }

        // See if the quote has a newline
        if (check_multiline(parser, end_quote)){
            snprintf(err, err_size-1, "Invalid file. Double quoted value was not terminated on the same line it began. Error on line: %ld", get_line_number(parser));
            PyErr_SetString(PyExc_ValueError, err);
            parser->token = NULL;
            return parser->token;
        }

        // Move the index 1 to skip the "
        parser->index++;
        return update_token(parser, end_quote);
    }

    // Nothing special. Just get the token
    long end_pos = get_next_whitespace(parser->full_data, parser->index);
    return update_token(parser, end_pos - parser->index);
}




static PyObject *
PARSE_load(PyObject *self, PyObject *args)
{
    char *file;

    if (!PyArg_ParseTuple(args, "s", &file))
        return NULL;

    // Read the file
    get_file(file, &parser);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
PARSE_load_string(PyObject *self, PyObject *args)
{
    char *data;

    if (!PyArg_ParseTuple(args, "s", &data))
        return NULL;

    // Read the string into our object
    reset_parser(&parser);
    parser.full_data = data;
    parser.length = strlen(data);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
PARSE_get_token(PyObject *self)
{
    char * token;
    token = get_token(&parser);

    // Pass errors up the chain
    if (token == NULL){
        return NULL;
    }

    // Return python none if done parsing
    if (token == done_parsing){
        Py_INCREF(Py_None);
        return Py_None;
    }

    return Py_BuildValue("s", token);
}

static PyObject *
PARSE_get_token_list(PyObject *self)
{
    PyObject * str;
    PyObject * list = PyList_New(0);
    if (!list)
        return NULL;

    char * token = get_token(&parser);
    // Pass errors up the chain
    if (token == NULL)
        return NULL;

    while (token != done_parsing){

        // Create a python string
        str = PyString_FromString(token);
        if (!str){
            return NULL;
        }
        if (PyList_Append(list, str) != 0){
            return NULL;
        }

        // Get the next token
        token = get_token(&parser);

        // Pass errors up the chain
        if (token == NULL)
            return NULL;

        // Otherwise we will leak memory
        Py_DECREF(str);
    }
    if (PyList_Reverse(list) != 0){
        return NULL;
    }

    return list;
}

static PyObject *
PARSE_get_line_no(PyObject *self)
{
    long line_no;
    line_no = get_line_number(&parser);

    return Py_BuildValue("l", line_no);
}

static PyObject *
PARSE_get_last_delineator(PyObject *self)
{
    return Py_BuildValue("c", parser.last_delineator);
}

static PyMethodDef cnmrstarparserMethods[] = {
    {"load",  (PyCFunction)PARSE_load, METH_VARARGS,
     "Load a file in preparation to parse."},

     {"load_string",  (PyCFunction)PARSE_load_string, METH_VARARGS,
     "Load a string in preparation to parse."},

     {"get_token",  (PyCFunction)PARSE_get_token, METH_NOARGS,
     "Get one token from the file. Returns NULL when file is exhausted."},

     {"get_token_list",  (PyCFunction)PARSE_get_token_list, METH_NOARGS,
     "Get all of the tokens as a list."},

     {"get_line_number",  (PyCFunction)PARSE_get_line_no, METH_NOARGS,
     "Get the line number of the last token."},

     {"get_last_delineator",  (PyCFunction)PARSE_get_last_delineator, METH_NOARGS,
     "Get the last token delineator."},

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
initcnmrstarparser(void)
{
    Py_InitModule3("cnmrstarparser", cnmrstarparserMethods,
                         "A NMR-STAR parser implemented in C.");
}
