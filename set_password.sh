echo -n "Password: " && read -s pw
echo "#ifndef PASSWORD_H" > password.h
echo "#define PASSWORD_H" >> password.h
echo "#define PASSWORD \"$pw\"" >> password.h
echo "#define PASSWORD_LEN ${#pw}" >> password.h
echo "#define PASSWORD_LEN_STR \"${#pw}\"" >> password.h
echo "#endif" >> password.h
cp password.h latch_control/
cp password.h latch_remote/
rm password.h
echo
echo " [DONE] Password set"