rm -rf build && mkdir build && cd build && cmake .. && make && make install && cd ..
echo "   ==== Uninstalling mplane yang module ===="
sysrepoctl --uninstall mplane
sleep 2
echo "   ==== Installing mplane.yang ===="
sysrepoctl --install modules/mplane.yang

kill -9 `lsof -t -i:830`

echo " =============== Starting server ================"
../netopeer2_run.sh --server
echo "============== Server stopped now ==============="

