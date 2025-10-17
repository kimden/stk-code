import stkclientpy
# available functions in stkclientpy for now:
# addCommand(cmd): Sends a command to the server, do not pass the preceding slash. Must be a server command.
# loadItemPolicyPreset(preset): Attempts to load the item policy preset "preset.xml", client-side, and send it to the server.
# privateMessage(str): Displays "str" locally only.
# publicMessage(str): Acts as if the user had types "str" in chat and send it themselves.


def stk_client_entry(cmd_str, player_str):
    args = cmd_str.split(" ")
    players = player_str.split(";")

    if len(args) < 3:
        stkclientpy.privateMessage("SetTurn ERROR: must provide a player to set their turn!")
        return None

    for p in players:
        if p == "" or p == " ":
            continue
        stkclientpy.addCommand("as {} spectate {}".format(p, int(p != args[2])))

    return None
