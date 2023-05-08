"""For Nu."""

# Import the Portal object.
import geni.portal as portal
# Import the ProtoGENI library.
import geni.rspec.pg as pg
# Import the Emulab specific extensions.
import geni.rspec.emulab as emulab

# Describe the parameter(s) this profile script can accept.
portal.context.defineParameter("n", "Number of Machines", portal.ParameterType.INTEGER, 8)
portal.context.defineParameter("node_type", "Node Type", portal.ParameterType.STRING, "c6525-100g")
portal.context.defineParameter("link_0_bw", "The bandwidth (GB/s) of link 0", portal.ParameterType.INTEGER, 25)
portal.context.defineParameter("link_1_bw", "The bandwidth (GB/s) of link 1", portal.ParameterType.INTEGER, 100)

# Retrieve the values the user specifies during instantiation.
params = portal.context.bindParameters()

# Create a portal object,
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

nodes = []
ifaces_0 = []
ifaces_1 = []

for i in range(0, params.n):
    n = request.RawPC('node-%d' % i)
    n.routable_control_ip = True
    n.hardware_type = params.node_type
    n.disk_image = 'urn:publicid:IDN+utah.cloudlab.us+image+shenango-PG0//nu-100g'
    nodes.append(n)
    iface_0 = n.addInterface('interface-%d' % (2 * i), pg.IPv4Address('10.10.1.%d' % (i + 1),'255.255.255.0'))
    ifaces_0.append(iface_0)
    iface_1 = n.addInterface('interface-%d' % (2 * i + 1), pg.IPv4Address('10.10.2.%d' % (i + 1),'255.255.255.0'))
    ifaces_1.append(iface_1)

# link 0
link_0 = request.Link('link-0')
link_0.Site('undefined')
link_0.best_effort = False
link_0.bandwidth = params.link_0_bw * 1000000
link_0.setNoInterSwitchLinks()
for iface in ifaces_0:
    link_0.addInterface(iface)

# link 1
link_1 = request.Link('link-1')
link_1.Site('undefined')
link_1.best_effort = False
link_1.bandwidth = params.link_1_bw * 1000000
link_1.setNoInterSwitchLinks()
for iface in ifaces_1:
    link_1.addInterface(iface)

# Print the generated rspec
pc.printRequestRSpec(request)
