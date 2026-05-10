#include <Thrift-Project.h>
#include <bits/getopt.h>
#include <getopt.h>
#include <stdio.h>
#include <wallet.h>
#include <zot.h>
#define _XOPEN_SOURCE 500

int getsubopt(char **optionp, char *const *keylistp, char **valuep);

#include "book.h"
#include "node_net.h"

void print_help() {
  printf("Usage:\n"
         "thrift [--thrift \"path/to/.thrift\"] --add-tx "
         "from=<sender>,to=<reciever>,amount=<amount>[,note=<note>]\n"
         "thrift [--thrift \"path/to/.thrift\"] --show-balances\n"
         "thrift [--bootstrap \"path/to/bootstrap\" --thrift "
         "\"path/to/.thrift/\"] --start-node "
         "address=<address>[,port=<port>]\n");
}

int main(int argc, char *argv[]) {
  printf("Thrift Project CLI\n"
         "Version %d.%d.%d\n",
         THRIFT_PROJECT_VERSION_MAJOR, THRIFT_PROJECT_VERSION_MINOR,
         THRIFT_PROJECT_VERSION_PATCH);

  node_context_t ctx = {nullptr};

  if (!parse_cli_args(argc, argv, &ctx.config)) {
    print_help();
  }

  if (ctx.config.thrift_path)
    set_thrift_home(ctx.config.thrift_path);
  if (ctx.config.bootstrap_path)
    set_bootstrap_path(ctx.config.bootstrap_path);

  if (__builtin_popcount(ctx.config.tasks) != 1) {
    print_help();
    return -1;
  }

  if (!load_book(&ctx.book)) {
    LOG_ERROR("Failed to load book.");
    return -1;
  }

  if (ctx.config.tasks == SHOW_BALS) {
    show_balances(&ctx.book);
    return 0;
  }

  ctx.user_keystore = init_keystore();
  if (!load_keystore(ctx.user_keystore)) {
    LOG_ERROR("Failed to load keystore.");
    return -1;
  }

  if (ctx.config.tasks == ADD_TX) {
    add_tx(&ctx, ctx.config.tx.to, ctx.config.tx.amount, ctx.config.tx.purpose,
           false);
    return 0;
  }

  if (ctx.config.tasks == CREATE_WALLET) {
    ctx.user_keystore = init_keystore();
    auto key_store = create_wallet(ctx.user_keystore);
    if (!key_store) {
      LOG_ERROR("Wallet creation failed.") return -1;
    }
    LOG("You address is: %s.", get_address_from_keystore(ctx.user_keystore));
    return 0;
  }

  if (ctx.config.tasks == START_NODE) {
    run_node(&ctx, ctx.config.address, ctx.config.port);
    printf("Node stopped.\n");
    return 0;
  }

  return 0;
}
