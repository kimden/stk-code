import stkbatchpy

def batch_entry(cmdraw, playersraw):
    cmd = cmdraw.decode("utf-8")
    playerstr = playersraw.decode("utf-8")
    args = cmd.split(" ")
    players = playerstr.split(";")

    if len(args) < 3:
        stkbatchpy.addMessage("SetTurn ERROR: must provide a player to set their turn!")
        return None

    for p in players:
        if p == "" or p == " ":
            continue

        if p == args[2]:
            stkbatchpy.addCommand("as " + p + " spectate 0")
        else:
            stkbatchpy.addCommand("as " + p + " spectate 1")

    return None
