gcc -E -C -P -I%1 "-D BOOST_USER_CONFIG=<%1/libs/mpl/preprocessed/include/%2/user.hpp>" -D BOOST_MPL_CFG_NO_OWN_PP_PRIMITIVES %3 >%4
