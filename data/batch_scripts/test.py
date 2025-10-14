import stkbatchpy

def batch_entry(cmd):
    mystr = cmd.decode("utf-8")
    stkbatchpy.addMessage("First " + mystr)
    stkbatchpy.addMessage("Second " + mystr)
    stkbatchpy.addCommand("spectate")
    return None
