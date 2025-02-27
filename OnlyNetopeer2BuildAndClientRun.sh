rm -rf build && mkdir build && cd build && cmake .. && make && make install && ldconfig && cd ..
cp -f src/sp/content-user-rpc.xml /tmp/

echo "   ==== Installing examples.yang ===="
sysrepoctl --install src/sp/examples.yang

echo " =============== Starting client ================"
#../netopeer2_run.sh --client
gdb build/netopeer2-cli
echo "============== Client disconnected now ==============="

