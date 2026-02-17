sudo cp ./register-rosetta.sh /usr/local/bin/register-rosetta.sh
sudo chmod +x /usr/local/bin/register-rosetta.sh
sudo cp ./rosetta-binfmt.service /etc/systemd/system/rosetta-binfmt.service
sudo systemctl daemon-reload
sudo systemctl enable rosetta-binfmt.service

# Restart forces the new register-rosetta.sh to run immediately
sudo systemctl restart rosetta-binfmt.service 
systemctl status rosetta-binfmt.service --no-pager

