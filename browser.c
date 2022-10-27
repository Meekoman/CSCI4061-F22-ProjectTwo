/* CSCI-4061 Fall 2022
 * Group Member #1: Amir Mohamed, moha1276
 * Group Member #2: Thomas Suiter, suite014
 * Group Member #3: Shannon Wallace, walla423
 */

#include "wrapper.h"
#include "util.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <signal.h>

#define MAX_TABS 100  // this gives us 99 tabs, 0 is reserved for the controller
#define MAX_BAD 1000
#define MAX_URL 100
#define MAX_FAV 100
#define MAX_LABELS 100 


comm_channel comm[MAX_TABS];         // Communication pipes 
char favorites[MAX_FAV][MAX_URL];    // Maximum char length of a url allowed
int num_fav = 0;                     // # favorites

typedef struct tab_list {
  int free;
  int pid; 
} tab_list;

tab_list TABS[MAX_TABS];  


/************************/
/* Simple tab functions */
/************************/

// return total number of tabs
int get_num_tabs () {
  int count = 0;
  for (int i = 1; i < MAX_TABS; i++) {
    if (!TABS[i].free) {
      count++;
    }
  }
  return count;
}

// get next free tab index, returns -1 if no tabs free
int get_free_tab () {
  for (int i = 1; i < MAX_TABS; i++) {
    if(TABS[i].free){
      return i;
    }
  }
  return -1; 
}

// init TABS data structure
void init_tabs () {
  int i;

  for (i = 1; i <= MAX_TABS; i++){
    TABS[i].free = 1;
  }
  TABS[0].free = 0;
}

/***********************************/
/* Favorite manipulation functions */
/***********************************/

// return 0 if favorite is ok, -1 otherwise
// both max limit, already a favorite (Hint: see util.h) return -1
int fav_ok (char *uri) {
  if(on_favorites(uri) == 1){ 
    return 1;
  }
  else if(num_fav == MAX_FAV){ 
    return 2;
  }
  else{ 
    return 0;
  }
}

// Add uri to favorites file and update favorites array with the new favorite
void update_favorites_file (char *uri) {

  FILE *f;
  f = fopen(".favorites", "a");
  if(f == NULL){
    perror("error with open\n");
    exit(-1);
  }

  if(fclose(f)) {
    perror("error with closing favorites\n");
    exit(-2);
  }


  strcpy(favorites[num_fav], uri);
  num_fav++;
}

// Set up favorites array
void init_favorites (char *fname) {
  //TODO: 
	FILE *f;
	f = fopen(fname, "r"); 
	if (f==NULL) {
    perror("error opening favorites\n");
 		exit(-1);
  }
    
  char url[MAX_URL]; 

  while (fgets(url, MAX_URL, f)) { 
    strtok(url, "\n");
    if(fav_ok(url) == 1){
      continue;
    }
    else if(fav_ok(url) == 2){
      break;
    }
    else{
      strcpy(favorites[num_fav],url);
      num_fav++;
    } 
  }  

  if (fclose(f)) {
  	perror("cannot close file\n");
    exit(-2);
  }
  return;
}

// Make fd non-blocking just as in class!
// Return 0 if ok, -1 otherwise
// Really a util but I want you to do it :-)
int non_block_pipe (int fd) {
  int nFlags;
  
  if ((nFlags = fcntl(fd, F_GETFL, 0)) < 0)
    return -1;
  if ((fcntl(fd, F_SETFL, nFlags | O_NONBLOCK)) < 0)
    return -1;
  return 0;
}

/***********************************/
/* Functions involving commands    */
/***********************************/

// Checks if tab is bad and url violates constraints; if so, return.
// Otherwise, send NEW_URI_ENTERED command to the tab on inbound pipe
void handle_uri (char *uri, int tab_index) {
  if (bad_format(uri)) {
    alert("URL bad format");
    return;
  }

  if (on_blacklist(uri)) {
    alert("URL on blacklist");
    return;
  }

  if (tab_index >= MAX_TABS) {
    alert("Tab number exceeds MAX_TABS");
    return;
  }

  req_t request;
  strcpy(request.uri, uri);
  request.type = NEW_URI_ENTERED;
  request.tab_index = tab_index;

  int err = write(comm[tab_index].inbound[1], &request, sizeof(req_t));
  if (err == -1) {
    perror("error writing to pipe");
    return;
  }
}


// A URI has been typed in, and the associated tab index is determined
// If everything checks out, a NEW_URI_ENTERED command is sent
void uri_entered_cb (GtkWidget* entry, gpointer data) {
  if(data == NULL) {	
    return;
  }

  char *uripoint;
  char uri[MAX_URL];
  int tab_index;
  
  tab_index = query_tab_id_for_request(entry, data);
 
  uripoint = get_entered_uri(entry);
  strncpy(uri, uripoint, MAX_URL);

  handle_uri(uri, tab_index);
}
  

// Called when + tab is hit
// Check tab limit ... if ok then do some heavy lifting (see comments)
// Create new tab process with pipes
void new_tab_created_cb (GtkButton *button, gpointer data) {
  if (data == NULL) {
    return;
  }

  if (get_free_tab() == -1) {
    alert("Max tabs reached\n");
    return;
  }

  int tab_idx = get_free_tab();

  if(pipe(comm[tab_idx].inbound) == -1){
    perror("pipe failed for inbound\n");
    exit(0);
  }
  if(pipe(comm[tab_idx].outbound) == -1){
    perror("pipe failed for outbound\n");
    exit(0);
  }

  non_block_pipe(comm[tab_idx].inbound[0]);
  non_block_pipe(comm[tab_idx].outbound[0]);

  int pid = fork();
  if(pid < 0){
    perror("Error forking\n");
  }
  if (pid == 0) {    
    // Note: render has different arguments now: tab_index, both pairs of pipe fd's
    // (inbound then outbound) -- this last argument will be 4 integers "a b c d"

    int in_r = comm[tab_idx].inbound[0];
    int in_w = comm[tab_idx].inbound[1];
    int out_r = comm[tab_idx].outbound[0];
    int out_w = comm[tab_idx].outbound[1];
    
    char index[5];
    char args[100];
    sprintf(index, "%d", tab_idx);
    sprintf(args, "%d %d %d %d", in_r, in_w, out_r, out_w);

    int exe = execl("./render", "render", index, args, NULL);
    if(exe != 0){
      printf("Failed to execute render. Error code: %d \n", exe);
    }
  }
  else{
   
    TABS[tab_idx].free = 0;
    TABS[tab_idx].pid = pid;
  }
}

// This is called when a favorite is selected for rendering in a tab
// Hint: you will use handle_uri ...
// However: you will need to first add "https://" to the uri so it can be rendered
// as favorites strip this off for a nicer looking menu
void menu_item_selected_cb (GtkWidget *menu_item, gpointer data) {

  if (data == NULL) {
    return;
  }
  
  // Note: For simplicity, currently we assume that the label of the menu_item is a valid url
  char *basic_uri = (char *)gtk_menu_item_get_label(GTK_MENU_ITEM(menu_item));

  char uri[MAX_URL];
  sprintf(uri, "https://%s", basic_uri);

  int tab_id = query_tab_id_for_request(menu_item, data);
  handle_uri(uri, tab_id);

  return;
}


// BIG CHANGE: the controller now runs an loop so it can check all pipes
// Long function
int run_control() {
  browser_window * b_window = NULL;
  int i, j, nRead;
  req_t req;

  create_browser(CONTROLLER_TAB, 0, G_CALLBACK(new_tab_created_cb),
		 G_CALLBACK(uri_entered_cb), &b_window, comm[0]);

  create_browser_menu(&b_window, &favorites, num_fav);
  
  while (1) {
    process_single_gtk_event();
    // Read from all tab pipes including private pipe (index 0)
    
    // to handle commands:
    // PLEASE_DIE (controller should die, self-sent): send PLEASE_DIE to all tabs
    // From any tab:
    //    IS_FAV: add uri to favorite menu (Hint: see wrapper.h), and update .favorites
    //    TAB_IS_DEAD: tab has exited, what should you do?

    // Loop across all pipes from VALID tabs -- starting from 0
    for (i=0; i<MAX_TABS; i++) {
      if (TABS[i].free) continue;
      nRead = read(comm[i].outbound[0], &req, sizeof(req_t));

      if (nRead == -1 && errno == EAGAIN) continue;
      if (nRead == -1 && errno != EAGAIN){
        perror("different error read failed\n");
        exit(1);
      }

      // Case 1: PLEASE_DIE
      if (nRead != -1 && req.type == PLEASE_DIE) {
        for (j = 1; j < MAX_TABS; j++) {
          if (TABS[j].free) continue;
          req_t treq;
          treq.type = PLEASE_DIE;
          treq.tab_index = j;
          if (write(comm[j].inbound[1], &treq, sizeof(req_t)) == -1) {
            perror("error writing PLEASE_DIE to pipe\n");
          }
        }
        for(j = 1; j < get_num_tabs(); j++)
          wait(NULL);
        exit(0); 
      }

      // Case 2: TAB_IS_DEAD
	    if (nRead != -1 && req.type == TAB_IS_DEAD) {
        if (kill(TABS[i].pid, SIGKILL) == -1) {
          perror("failed call\n");
          exit(-1);
        }
        int exitstatus;
        if (waitpid(TABS[i].pid, &exitstatus, 0) == -1) {
          perror("failed wait\n");
          exit(1);
        }
        TABS[i].free = 1; 
      }

      // Case 3: IS_FAV
      if (nRead != -1 && req.type == IS_FAV) {
        char *uri = req.uri;
        if (fav_ok(uri) == 1) {
          alert("Already a favorite");
        } 
        else if(fav_ok(uri) == 2){
          alert("No more favorites can be added");
        }
        else {
         add_uri_to_favorite_menu(b_window, uri); 
         update_favorites_file(uri);  
        }
      }
      if(nRead != -1){
        fprintf(stderr, "Please provide command in the controller\n");
      }
    }
    usleep(1000);
  }
  return 0;
}


int main(int argc, char **argv)
{

  if (argc != 1) {
    fprintf (stderr, "browser <no_args>\n");
    exit (0);
  }

  init_tabs ();
  init_blacklist(".blacklist");
  init_favorites(".favorites");

  int pid = fork();
  if(pid < 0){
    perror("Error forking.\n");
  }

  if(pid == 0){
    if(pipe(comm[0].outbound) == -1){
      perror("pipe failed\n");
      exit(0);
    }
    non_block_pipe(comm[0].outbound[0]); 
    run_control();
  }

  else{
    if(wait(NULL) == -1){
      perror("Parent didn't wait.\n");
      exit(-1);
    }
    exit(0);
  }
}
