rm -rf build && mkdir build && cd build && cmake .. && make && make install && cd ..
echo "   ==== Uninstalling mplane yang module ===="
sysrepoctl --uninstall examples
sleep 2
echo "   ==== Installing examples.yang ===="
sysrepoctl --install sp/examples.yang

kill -9 `lsof -t -i:830`

cp -f sp/content-user-rpc.xml /tmp/

echo " =============== Starting server ================"
../netopeer2_run.sh --server
echo "============== Server stopped now ==============="

