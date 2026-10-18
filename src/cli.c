#include "cli.h"
#include "book.h"
#include <Thrift-Project.h>
#include <bits/getopt.h>
#include <bits/ip_mreq_source.h>
#include <getopt.h>
#include <stdint.h>
#include <wallet.h>
#include <zot.h>
#define _XOPEN_SOURCE 500
#include <stdlib.h>

#define DEFAULT_PORT (80)

int getsubopt(char **optionp, char *const *keylistp, char **valuep);

enum { TO = 0, AMOUNT, NOTE, ADDRESS, PORT, THRIFT };
char *const tokens[] = {
    [TO] = "to",           [AMOUNT] = "amount", [NOTE] = "note",
    [ADDRESS] = "address", [PORT] = "port",     [THRIFT] = "thrift_path"};

bool strtotft(const char *str, uint32_t scale, int64_t *out) {
  if (!str || !out || scale > 18)
    return false;

  const char *p = str;
  if (*p == '-')
    return false;
  if (*p < '0' || *p > '9')
    return false;

  int64_t acc = 0;
  int frac = 0;
  bool seen_dot = false;

  for (; *p; p++) {
    if (*p == '.') {
      if (seen_dot)
        return false;
      seen_dot = true;
      continue;
    }
    if (*p < '0' || *p > '9')
      return false;

    if (seen_dot) {
      if (frac == (int)scale) {
        if (*p != '0')
          return false;
        continue;
      }
      frac++;
    }

    int digit = *p - '0';
    if (acc > (INT64_MAX - digit) / 10)
      return false;
    acc = (acc * 10) + digit;
  }

  while (frac < (int)scale) {
    if (acc > INT64_MAX / 10)
      return false;
    acc *= 10;
    frac++;
  }

  *out = acc;
  return true;
}

bool parse_cli_args(int argc, char *argv[], node_config_t *config) {

  if (argc < 2) {
    return false;
  }

  static struct option long_options[] = {
      {"add-tx", required_argument, nullptr, 'a'},
      {"add-reward", no_argument, nullptr, 'r'},
      {"show-balances", no_argument, nullptr, 'b'},
      {"create-wallet", no_argument, nullptr, 'c'},
      {"start-node", required_argument, nullptr, 's'},
      {"thrift", required_argument, nullptr, 'T'},
      {"miner", required_argument, nullptr, 'M'},
      {"bootstrap", required_argument, nullptr, 'B'},
      {nullptr, 0, nullptr, 0}};

  int opt_indx = 0;
  int short_opt = 0;

  while ((short_opt = getopt_long(argc, argv, "a:rbcs:T:M:B:", long_options,
                                  &opt_indx)) != -1) {
    switch (short_opt) {
    case 'a': {
      char *subopt = optarg;
      char *value;
      while (*subopt != '\0') {
        switch (getsubopt(&subopt, tokens, &value)) {
        case TO:
          config->tx.to = value;
          break;
        case AMOUNT:
          if (!strtotft(value, UNIT_SCALE_DIGITS, &config->tx.amount)) {
            LOG_ERROR("Ill formed amount");
          }
          break;
        case NOTE:
          config->tx.purpose = value;
          break;
        }
      }
      config->tasks |= ADD_TX;
    } break;
    case 'b': {
      config->tasks |= SHOW_BALS;
    } break;
    case 'c': {
      config->tasks |= CREATE_WALLET;
    } break;
    case 's': {
      config->port = DEFAULT_PORT;
      config->address = nullptr;
      char *subopt = optarg;
      char *value;
      while (*subopt != '\0') {
        switch (getsubopt(&subopt, tokens, &value)) {
        case ADDRESS:
          config->address = value;
          break;
        case PORT:
          config->port = atoi(value);
          break;
        }
      }
      config->tasks |= START_NODE;
    } break;
    case 'T': {
      config->thrift_path = zstrdup(optarg);
    } break;
    case 'M': {
      config->miner = zstrdup(optarg);
    } break;
    case 'B': {
      config->bootstrap_path = zstrdup(optarg);
    } break;
    }
  }

  return true;
}
