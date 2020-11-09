/**
 * \file splitdefines.h
 *
 * \brief Common defines and constant values used by the 'split' module.
 *
 * Outsourced into additional file to provide slim integration into existing
 * source code via the 'include'-system
 **/

#ifndef TOR_SPLITDEFINES_H
#define TOR_SPLITDEFINES_H

#include "lib/cc/torint.h"

/*** BUILD CONFIGURATION ***/
/* uncomment for enabling creation of exclude list for new split circuits */
#define SPLIT_GENERATE_EXCLUDE

/* uncomment for preventing Tor from building circuits preemptively */
/* If you start Tor in a server mode, keep the configuration line commented. */
/* If you start Tor in a client mode, better keep the configuration line uncommented. */
#define SPLIT_DISABLE_PREEMPTIVE_CIRCUITS

/* uncomment to disable Nagle's Algorithm for TCP sockets */
#define SPLIT_DISABLE_NAGLE

/* uncomment for forcing Tor to launch a new circuit for each new SOCKS
 * connection */
#define SPLIT_SOCKS_LAUNCH_NEW_CIRCUIT

/* null-terminated string of the default interface to use for new
 * split circuits (provide an empty string "" to allow the use of
 * arbitrary interfaces) */
#define SPLIT_DEFAULT_INTERFACE ""

/*** DEFINES ***/

/* length of the used cookie in bytes (oriented at REND_COOKIE_LEN) */
#define SPLIT_COOKIE_LEN 20

/* maximum number of sub-circuits per circuit */
#define MAX_SUBCIRCS 5

/* default number of sub-circuits we want to establish per circuit */
#define SPLIT_DEFAULT_SUBCIRCS 3

/* number of primary guards that must be choosen at minimum */
#define SPLIT_MIN_NUM_PRIMARY_GUARDS 2 + SPLIT_DEFAULT_SUBCIRCS

/* circuits that are built to join an existing split circuit shall have a
   route length of 2 (entry guard -> merging middle) */
#define SPLIT_DEFAULT_ROUTE_LEN 2

/* split strategy that is used as default by new split circuits
 * (refers to enum split_strategy_t in file splitstrategy.h) */
#define SPLIT_DEFAULT_STRATEGY SPLIT_STRATEGY_ROUND_ROBIN

/* maximum number of split instructions that can be stored in one direction */
#define MAX_NUM_SPLIT_INSTRUCTIONS 8

/* number of split instructions to send when finalising a split circuit
 * (must be smaller than MAX_NUM_SPLIT_INSTRUCTIONS) */
#define NUM_SPLIT_INSTRUCTIONS 2

/*** TYPEDEFS ***/

typedef struct split_data_t split_data_t;
typedef struct split_data_client_t split_data_client_t;
typedef struct split_data_or_t split_data_or_t;
typedef struct split_data_circuit_t split_data_circuit_t;
typedef enum split_cookie_state_t split_cookie_state_t;
typedef struct subcircuit_t subcircuit_t;
typedef enum subcirc_state_t subcirc_state_t;
typedef struct split_instruction_t split_instruction_t;
typedef enum instruction_type_t instruction_type_t;
typedef enum split_strategy_t split_strategy_t;

#ifdef TOR_UNIT_TESTS
/* Always use 2 byte sub-circuit IDs for unit tests */
typedef uint16_t subcirc_id_t;
#else /* TOR_UNIT_TESTS */

#if MAX_SUBCIRCS <= (1 << 8)
typedef uint8_t subcirc_id_t;
#elif MAX_SUBCIRCS <= (1<<16)
typedef uint16_t subcirc_id_t;
#else
#error "Configured MAX_SUBCIRCS is unsupoorted"
#endif

#endif /*TOR_UNIT_TESTS */

#endif /* TOR_SPLITDEFINES_H */
