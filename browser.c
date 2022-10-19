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
  int pid; // may or may not be useful
} tab_list;

// Tab bookkeeping
tab_list TABS[MAX_TABS];  


/************************/
/* Simple tab functions */
/************************/

// return total number of tabs
int get_num_tabs () {
  int count = 0;
  for (int i=0; i < MAX_TABS; i++) {
    if (TABS[i].free == 1) {
      count++;
    }
  }
  return count;
}

// get next free tab index
int get_free_tab () {
  return 1;
}

// init TABS data structure
void init_tabs () {
  int i;

  for (i=1; i<MAX_TABS; i++)
    TABS[i].free = 1;
  TABS[0].free = 0;
}

/***********************************/
/* Favorite manipulation functions */
/***********************************/

// return 0 if favorite is ok, -1 otherwise
// both max limit, already a favorite (Hint: see util.h) return -1
int fav_ok (char *uri) {
  return 0;
}


// Add uri to favorites file and update favorites array with the new favorite
void update_favorites_file (char *uri) {
  // Add uri to favorites file

  // Update favorites array with the new favorite
}

// Set up favorites array
void init_favorites (char *fname) {
  //TODO: 
}

// Make fd non-blocking just as in class!
// Return 0 if ok, -1 otherwise
// Really a util but I want you to do it :-)
int non_block_pipe (int fd) {
  return 0;
}

/***********************************/
/* Functions involving commands    */
/***********************************/

// Checks if tab is bad and url violates constraints; if so, return.
// Otherwise, send NEW_URI_ENTERED command to the tab on inbound pipe
void handle_uri (char *uri, int tab_index) {
  if (bad_format(uri)) {
    perror("URL bad format");
    return;
  }

  if (on_blacklist(uri)) {
    perror("URL on blacklist");
    return;
  }

  if (tab_index > MAX_TABS)
    perror("Tab number exceeds MAX_TABS");
    return;

  write(comm[tab_index].outbound[1], 
        uri, (sizeof (char)*MAX_URL));
}


// A URI has been typed in, and the associated tab index is determined
// If everything checks out, a NEW_URI_ENTERED command is sent (see Hint)
// Short function
void uri_entered_cb (GtkWidget* entry, gpointer data) {
  
  if(data == NULL) {	
    return;
  }

  char *uripoint;
  int tab_index;

  // Get the tab (hint: wrapper.h)
  tab_index = query_tab_id_for_request(entry, data);

  // Get the URL (hint: wrapper.h)
  uripoint = get_entered_uri(entry);

  // Hint: now you are ready to handle_the_uri
  handle_uri(uripoint, tab_index);
}
  

// Called when + tab is hit
// Check tab limit ... if ok then do some heavy lifting (see comments)
// Create new tab process with pipes
// Long function
void new_tab_created_cb (GtkButton *button, gpointer data) {
  
  if (data == NULL) {
    return;
  }

  // at tab limit?
  if (get_num_tabs()>MAX_TABS) {
    return;
  }

  // Get a free tab
  int tab_idx = get_free_tab();

  // Create communication pipes for this tab
  pipe(comm[tab_idx].inbound);
  pipe(comm[tab_idx].outbound);

  // Make the read ends non-blocking 
  
  
  // fork and create new render tab
  if (fork() == 0) {
    perror("child process for tab has arrived");
    
  // Note: render has different arguments now: tab_index, both pairs of pipe fd's
  // (inbound then outbound) -- this last argument will be 4 integers "a b c d"
  // Hint: stringify args
    int in_r = comm[tab_idx].inbound[0];
    int in_w = comm[tab_idx].inbound[1];
    int out_r = comm[tab_idx].outbound[0];
    int out_w = comm[tab_idx].outbound[1];
    
    char args[100]; // TODO:  change this later to a value that is more limited
    sprintf(args, " %d %d %d %d", in_r, in_w, out_r, out_w);
    execl("./render", args);
  }

  // Controller parent just does some TABS bookkeeping
}

// This is called when a favorite is selected for rendering in a tab
// Hint: you will use handle_uri ...
// However: you will need to first add "https://" to the uri so it can be rendered
// as favorites strip this off for a nicer looking menu
// Short
void menu_item_selected_cb (GtkWidget *menu_item, gpointer data) {

  if (data == NULL) {
    return;
  }
  
  // Note: For simplicity, currently we assume that the label of the menu_item is a valid url
  // get basic uri
  char *basic_uri = (char *)gtk_menu_item_get_label(GTK_MENU_ITEM(menu_item));

  // append "https://" for rendering
  char uri[MAX_URL];
  sprintf(uri, "https://%s", basic_uri);

  // Get the tab (hint: wrapper.h)
  int tab_id = query_tab_id_for_request(menu_item, data);

  // Hint: now you are ready to handle_the_uri
  handle_uri(basic_uri, tab_id);

  return;
}


// BIG CHANGE: the controller now runs an loop so it can check all pipes
// Long function
int run_control() {
  browser_window * b_window = NULL;
  int i, nRead;
  req_t req;

 
  
  //Create controller window
  create_browser(CONTROLLER_TAB, 0, G_CALLBACK(new_tab_created_cb),
		 G_CALLBACK(uri_entered_cb), &b_window, comm[0]);

  // Create favorites menu
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

      // Check that nRead returned something before handling cases
      if (nRead != 0) continue;

      // Case 1: PLEASE_DIE

      // Case 2: TAB_IS_DEAD
	    
      // Case 3: IS_FAV
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
  // init blacklist (see util.h), and favorites (write this, see above)
  init_blacklist(".blacklist");
  init_favorites(".favorites");

  // Fork controller
  while (fork()==0) {
    // Child creates a pipe for itself comm[0]
    pipe(comm[0].inbound);
    pipe(comm[0].outbound);

    printf("this is the child process speaking \n");
    // then calls run_control ()
    int i = run_control();
    printf("run status is %d", i);
  }
  
  // Parent waits ...
  wait(0);
}
