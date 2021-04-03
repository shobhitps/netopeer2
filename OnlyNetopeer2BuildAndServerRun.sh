rm -rf build && mkdir build && cd build && cmake .. && make && make install && cd ..
echo "   ==== Uninstalling examples yang module ===="
sysrepoctl --uninstall examples
sleep 2
echo "   ==== Installing examples.yang ===="
sysrepoctl --install src/sp/examples.yang
kill -9 `lsof -t -i:830`
sleep 2
echo " =============== Starting server ================"
../netopeer2_run.sh --server
echo "============== Server stopped now ==============="

