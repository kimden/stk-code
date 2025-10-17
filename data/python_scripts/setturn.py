import stkclientpy

def stk_client_entry(cmd_str, player_str):
    args = cmd_str.split(" ")
    players = player_str.split(";")

    if len(args) < 3:
        stkclientpy.addMessage("SetTurn ERROR: must provide a player to set their turn!")
        return None

    for p in players:
        if p == "" or p == " ":
            continue
        stkclientpy.addCommand("as {} spectate {}".format(p, int(p != args[2])))

    return None
