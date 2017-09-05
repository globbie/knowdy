import sys
import zmq
import datetime
import random
import string

GLB_COLL_ADDR = "tcp://127.0.0.1:6908"
MAX_USERS = 1000

MOCKUP_USER_NAME_SIZE = 16
MOCKUP_DOMAIN_SIZE = 8
MOCKUP_SALT_SIZE = 32
MOCKUP_HASH_SIZE = 64
MOCKUP_PHONE_PREFIX_SIZE = 3
MOCKUP_PHONE_NUM_SIZE = 7

task_template = """{task
  {tid %s}
  {user{auth {sid AUTH_SERVER_SID}}
  {class User
  %s}}}"""

def add_random_user(rec, count):
    user_name = ''.join(random.choices(string.ascii_uppercase + string.digits, k=MOCKUP_USER_NAME_SIZE))
    random_domain = ''.join(random.choices(string.ascii_lowercase, k=MOCKUP_DOMAIN_SIZE))
    user_email = user_name + "@" + random_domain + ".com"
    
    pass_salt = ''.join(random.choices(string.ascii_lowercase + string.digits, k=MOCKUP_SALT_SIZE))
    pass_hash = ''.join(random.choices(string.ascii_lowercase + string.digits, k=MOCKUP_HASH_SIZE))

    phone_prefix = ''.join(random.choices(string.digits, k=MOCKUP_PHONE_PREFIX_SIZE))
    phone_num = ''.join(random.choices(string.digits, k=MOCKUP_PHONE_NUM_SIZE))
    
    rec.append("(obj %s" % user_name)
    rec.append("  (contacts (mail %s) (phone (prefix %s)(number %s)))" % (user_email, phone_prefix, phone_num))
    rec.append("  (ident (id %d)(salt %s)" % (count, pass_salt))
    rec.append("         (hash %s))" % pass_hash)
    rec.append(")")

if __name__ == "__main__":
    ctx = zmq.Context()
    socket = ctx.socket(zmq.PUSH)
    socket.connect(GLB_COLL_ADDR)
    # user id count
    i = 0
    rec = []
    tid = "123456"
    a = datetime.datetime.now()

    while i < MAX_USERS:
        i += 1
        add_random_user(rec, i)

    s = "\n".join(rec)
    task = task_template % (tid, s)
    print(task)

    b = datetime.datetime.now()

    print("TASK GENERATION TIME: %s" % (b - a))
    
    msgs = []
    msgs.append(task.encode("UTF-8"))
    msgs.append("TPS TEST".encode("UTF-8"))
    a = datetime.datetime.now()
    socket.send_multipart(msgs)
    socket.close()
    b = datetime.datetime.now()

    print("TASK SEND TIME: %s" % (b - a))
 
